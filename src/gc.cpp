#include "sharpscript/gc.hpp"

#include "sharpscript/value.hpp"

namespace sharpscript {
namespace {

bool hasFlag(GcFlag value, GcFlag flag) {
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0u;
}

}  // namespace

GcObject::~GcObject() {
    if (gcCollector_ != nullptr) {
        gcCollector_->unregisterObject(this);
        gcCollector_ = nullptr;
    }
}

GcCollector::~GcCollector() { shutdown(); }

void GcCollector::registerObject(const std::shared_ptr<GcObject>& object) {
    if (!object || shuttingDown_) return;
    if (object->gcRegistered_) {
        if (object->gcCollector_ == nullptr) object->gcCollector_ = this;
        return;
    }
    object->gcCollector_ = this;
    object->gcRegistered_ = true;
    object->gcFlag_ = false;
    object->gcInternalReferences_ = 0;
    objects_[object.get()] = std::weak_ptr<GcObject>(object);
    ++stats_.totalNewObjects;
    ++stats_.allocationsSinceLastCycle;
}

void GcCollector::refreshRegistration(const std::shared_ptr<GcObject>& object) {
    if (!object || shuttingDown_) return;
    object->gcCollector_ = this;
    object->gcRegistered_ = true;
    objects_[object.get()] = std::weak_ptr<GcObject>(object);
}

void GcCollector::unregisterObject(GcObject* object) {
    if (shuttingDown_ || object == nullptr) return;
    auto it = objects_.find(object);
    if (it == objects_.end()) return;
    objects_.erase(it);
    ++stats_.totalDestroyed;
}

void GcCollector::enumerateValue(const Value& value) {
    switch (value.kind()) {
        case ValueKind::Object:
        case ValueKind::Array:
        case ValueKind::Struct:
        case ValueKind::Delegate:
        case ValueKind::Cell:
        case ValueKind::Ref: {
            GcObject* object = value.gcObject();
            if (object != nullptr) referenceFound(object);
            break;
        }
        default: break;
    }
}

void GcCollector::referenceFound(GcObject* object) {
    if (object == nullptr || object->gcCollector_ != this) return;
    switch (phase_) {
        case Phase::Detect:
            // AngelScript's gcCount: references that stay inside the candidate set.
            if (object->gcFlag_) ++object->gcInternalReferences_;
            break;
        case Phase::Propagate:
            // Reached from an object with an outside reference: it is alive.
            if (object->gcFlag_) {
                object->gcFlag_ = false;
                aliveQueue_.push_back(object);
            }
            break;
        default: break;
    }
}

bool GcCollector::beginCycle() {
    cycleObjects_.clear();
    aliveQueue_.clear();
    cursor_ = 0;
    cycleWorkMs_ = 0.0;

    for (auto it = objects_.begin(); it != objects_.end();) {
        auto object = it->second.lock();
        if (!object) {
            it = objects_.erase(it);
            continue;
        }
        object->gcFlag_ = true;
        object->gcInternalReferences_ = 0;
        cycleObjects_.push_back(std::move(object));
        ++it;
    }

    stats_.allocationsSinceLastCycle = 0;
    if (cycleObjects_.empty()) {
        phase_ = Phase::Idle;
        return false;
    }
    phase_ = Phase::Detect;
    return true;
}

bool GcCollector::step(std::size_t budget) {
    if (phase_ == Phase::Detect) {
        std::size_t processed = 0;
        while (cursor_ < cycleObjects_.size() && processed < budget) {
            cycleObjects_[cursor_]->gcEnumerateReferences(*this);
            ++cursor_;
            ++processed;
        }
        if (cursor_ < cycleObjects_.size()) return false;

        // An object whose use count exceeds its internal references is also
        // referenced from outside the cycle (global, stack, host): a root.
        for (auto& object : cycleObjects_) {
            const auto useCount = static_cast<long long>(object.use_count());
            const auto internal = static_cast<long long>(object->gcInternalReferences());
            if (useCount - 1 > internal) {
                object->gcFlag_ = false;
                aliveQueue_.push_back(object.get());
            }
        }
        cursor_ = 0;
        phase_ = Phase::Propagate;
    }

    if (phase_ == Phase::Propagate) {
        std::size_t processed = 0;
        while (!aliveQueue_.empty() && processed < budget) {
            GcObject* object = aliveQueue_.back();
            aliveQueue_.pop_back();
            object->gcEnumerateReferences(*this);
            ++processed;
        }
        if (!aliveQueue_.empty()) return false;
        phase_ = Phase::Destroy;
    }

    if (phase_ == Phase::Destroy) {
        finishCycle();
        return true;
    }
    return phase_ == Phase::Idle;
}

void GcCollector::finishCycle() {
    std::vector<GcObject*> garbage;
    for (auto& object : cycleObjects_) {
        if (object->gcFlag_) garbage.push_back(object.get());
    }

    // Hand finalizable objects to the engine *before* their edges are dropped,
    // so a finalizer observes the same fields a reference-counted destruction
    // would (C# runs finalizers with the object intact). The engine keeps them
    // alive until the finalizer ran and then drops their references itself.
    if (finalizerSink) {
        for (auto& object : cycleObjects_) {
            if (!object->gcFlag_ || !object->gcNeedsFinalizer()) continue;
            object->markFinalizerScheduled();
            finalizerSink(object);
        }
    }

    // Drop the internal edges. The collector's own handles keep these objects
    // alive, so no destructor runs while the graph is being torn down.
    for (GcObject* object : garbage) {
        if (object->gcFinalizerScheduled()) continue;
        object->gcReleaseReferences();
    }

    const std::size_t detected = garbage.size();
    if (detected > 0) {
        stats_.totalDetected += detected;
        stats_.totalDestroyedByCollector += detected;
    }
    stats_.lastCycleDetected = detected;
    ++stats_.cycles;

    aliveQueue_.clear();
    cursor_ = 0;
    phase_ = Phase::Idle;
    // Releasing the handles destroys the garbage (their use count is now only
    // the collector's) and leaves the reachable objects untouched.
    cycleObjects_.clear();
}

bool GcCollector::collect(GcFlag flag, std::size_t stepSize) {
    if (shuttingDown_) return true;

    const auto start = std::chrono::steady_clock::now();
    if (phase_ == Phase::Idle) {
        if (!beginCycle()) return true;
    }

    const bool fullCycle = hasFlag(flag, GcFlag::FullCycle);
    const std::size_t budget = fullCycle
                                   ? std::numeric_limits<std::size_t>::max()
                                   : (stepSize == 0 ? std::size_t{64} : stepSize);
    const bool finished = step(budget);

    cycleWorkMs_ += millisecondsSince(start);
    if (finished) {
        stats_.lastCycleMs = cycleWorkMs_;
        cycleWorkMs_ = 0.0;
    }
    return finished;
}

GcStatistics GcCollector::statistics() const {
    GcStatistics result = stats_;
    result.currentAllocated = objects_.size();
    return result;
}

void GcCollector::shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    for (auto& entry : objects_) {
        if (auto object = entry.second.lock()) object->gcCollector_ = nullptr;
    }
    objects_.clear();
    aliveQueue_.clear();
    phase_ = Phase::Idle;
    // Detached objects no longer unregister themselves; releasing the pins may
    // destroy garbage accrued by an interrupted incremental cycle.
    cycleObjects_.clear();
}

}  // namespace sharpscript
