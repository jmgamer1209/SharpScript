#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>

#include "common.hpp"

namespace sharpscript {

class GcCollector;
class Value;

/// Base class for heap objects owned by scripts (script instances and arrays).
///
/// This is the equivalent of AngelScript's asCScriptObject with respect to
/// garbage collection: the object takes part in reference counting (provided by
/// std::shared_ptr's control block) and exposes the two hooks the cycle
/// collector needs (`gcEnumerateReferences` / `gcReleaseReferences`, i.e.
/// AngelScript's EnumReferences / ReleaseAllReferences).
class GcObject {
public:
    GcObject() = default;
    virtual ~GcObject();

    GcObject(const GcObject&) = delete;
    GcObject& operator=(const GcObject&) = delete;

    /// Visits every reference held by this object. Called by the collector.
    virtual void gcEnumerateReferences(GcCollector& collector) = 0;
    /// Drops every outgoing reference so a cycle can be broken.
    virtual void gcReleaseReferences() = 0;
    /// True when the object must run a script finalizer before being destroyed.
    virtual bool gcNeedsFinalizer() const { return false; }
    /// True once the collector handed the object to the engine for finalization.
    bool gcFinalizerScheduled() const { return gcFinalizerScheduled_; }
    void markFinalizerScheduled() { gcFinalizerScheduled_ = true; }

    GcCollector* gcCollector() const { return gcCollector_; }
    /// True while the object is a garbage candidate in the running cycle.
    bool gcFlag() const { return gcFlag_; }
    /// Number of references pointing at this object from inside the cycle.
    int gcInternalReferences() const { return gcInternalReferences_; }

private:
    friend class GcCollector;

    GcCollector* gcCollector_ = nullptr;
    bool gcRegistered_ = false;
    bool gcFlag_ = false;
    bool gcFinalizerScheduled_ = false;
    int gcInternalReferences_ = 0;
};

enum class GcFlag : unsigned {
    None = 0,
    /// Run the cycle to completion in one call.
    FullCycle = 1u << 0,
    /// Advance the cycle by at most `stepSize` objects, keeping state between calls.
    OneStep = 1u << 1,
};

/// Snapshot of the collector state, mirroring AngelScript's GetGCStatistics.
struct GcStatistics {
    /// Objects currently registered with the collector (alive script objects).
    std::size_t currentAllocated = 0;
    /// Objects allocated since the engine was created.
    std::size_t totalNewObjects = 0;
    /// Objects destroyed so far, both by reference counting and by the collector.
    std::size_t totalDestroyed = 0;
    /// Objects detected as garbage (cycles) over all completed cycles.
    std::size_t totalDetected = 0;
    /// Objects destroyed by the collector because they were part of a cycle.
    std::size_t totalDestroyedByCollector = 0;
    /// Allocations since the last cycle started; drives automatic collection.
    std::size_t allocationsSinceLastCycle = 0;
    /// Number of completed cycles.
    std::size_t cycles = 0;
    /// Objects detected as garbage in the most recent cycle.
    std::size_t lastCycleDetected = 0;
    /// Time spent inside the collector during the most recent cycle.
    double lastCycleMs = 0.0;
};

/// Reference counting + incremental cycle collector.
///
/// Reference counting itself is done by std::shared_ptr, so objects without
/// cycles are destroyed immediately when their last reference goes away. The
/// collector only looks for cycles:
///
///  1. every registered object becomes a candidate (gcFlag = true) and its
///     internal reference counter is cleared;
///  2. each candidate enumerates its outgoing references; references to other
///     candidates increment their internal counter (AngelScript's gcCount);
///  3. an object whose shared_ptr use_count is greater than its internal count
///     is referenced from the outside (globals, stack, host) and is a root;
///     reachability is propagated from those roots;
///  4. candidates that were never marked reachable only reference each other,
///     so their outgoing references are dropped and they are destroyed.
class GcCollector {
public:
    GcCollector() = default;
    ~GcCollector();

    GcCollector(const GcCollector&) = delete;
    GcCollector& operator=(const GcCollector&) = delete;

    /// Creates a script object and registers it with this collector.
    ///
    /// The custom deleter is what makes script finalizers possible: when the
    /// last reference goes away the object is *revived* with a fresh control
    /// block and handed to `finalizerSink` instead of being destroyed, so the
    /// finalizer can run later with a valid reference (C# semantics).
    template <typename T, typename... Args>
    std::shared_ptr<T> make(Args&&... args) {
        auto* raw = new T(std::forward<Args>(args)...);
        std::shared_ptr<T> object(raw, [this](T* dying) { release(dying); });
        registerObject(std::static_pointer_cast<GcObject>(object));
        return object;
    }

    /// Receives objects whose last reference was dropped but that still need a
    /// finalizer. The receiver owns the reference and destroys it afterwards.
    std::function<void(std::shared_ptr<GcObject>)> finalizerSink;

    void registerObject(const std::shared_ptr<GcObject>& object);
    void unregisterObject(GcObject* object);
    /// Re-points the collector's weak reference after an object was revived with
    /// a new control block.
    void refreshRegistration(const std::shared_ptr<GcObject>& object);

    /// Runs (or advances) a collection cycle. Returns true when the collector
    /// has no work left, i.e. the current cycle (if any) has finished.
    bool collect(GcFlag flag = GcFlag::FullCycle, std::size_t stepSize = 0);

    GcStatistics statistics() const;
    bool busy() const { return phase_ != Phase::Idle; }
    /// Detaches all objects; safe to call from the owning engine's destructor.
    void shutdown();

    /// Callback invoked by gcEnumerateReferences (AngelScript's GCEnumCallback).
    void referenceFound(GcObject* object);
    /// Enumerates an object/array held by a Value without copying the handle.
    void enumerateValue(const Value& value);

private:
    enum class Phase { Idle, Detect, Propagate, Destroy };

    bool beginCycle();
    bool step(std::size_t budget);
    void finishCycle();

    /// Deleter used by `make`: either hands the object to `finalizerSink`
    /// (reviving it) or destroys it.
    template <typename T>
    void release(T* object) {
        if (finalizerSink && !shuttingDown_ && object->gcNeedsFinalizer() &&
            !object->gcFinalizerScheduled()) {
            std::shared_ptr<T> revived(object);
            refreshRegistration(std::static_pointer_cast<GcObject>(revived));
            finalizerSink(std::static_pointer_cast<GcObject>(revived));
            return;   // ownership moved to the sink
        }
        delete object;
    }

    std::unordered_map<GcObject*, std::weak_ptr<GcObject>> objects_;
    /// Strong references to every candidate of the running cycle. They keep the
    /// graph stable across incremental steps and are dropped at the end.
    std::vector<std::shared_ptr<GcObject>> cycleObjects_;
    std::vector<GcObject*> aliveQueue_;
    std::size_t cursor_ = 0;
    Phase phase_ = Phase::Idle;
    bool shuttingDown_ = false;
    double cycleWorkMs_ = 0.0;
    GcStatistics stats_;
};

}  // namespace sharpscript
