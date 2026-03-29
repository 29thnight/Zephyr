// Part of src/zephyr.cpp — included by zephyr.cpp
struct GcObject;
enum class HandleContainerKind : std::uint8_t {
    Stack,
    Global,
    HeapField,
    ArrayElement,
    ClosureCapture,
    CoroutineFrame,
    Serialize,
};

struct HostHandleToken {
    std::uint32_t slot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    bool valid() const { return slot != std::numeric_limits<std::uint32_t>::max() && generation != 0; }
};

enum class GcColor : std::uint8_t {
    White,
    Gray,
    Black,
};

inline const char* gc_color_name(GcColor color) {
    switch (color) {
        case GcColor::White:
            return "White";
        case GcColor::Gray:
            return "Gray";
        case GcColor::Black:
            return "Black";
    }
    return "Unknown";
}

enum GcFlags : std::uint8_t {
    GcDirtyQueuedBit     = 1 << 0,
    GcMinorRememberedBit = 1 << 1,
    GcFinalizableBit     = 1 << 2,
    GcPinnedBit          = 1 << 3,
    GcOldBit             = 1 << 4,
    GcBumpAllocBit       = 1 << 5,  // Phase 5C: object was placement-new'd into a NurserySpace bump chunk
    GcMovableBit         = 1 << 6,  // Phase 1A: reserved for future moving sub-spaces
    GcSlabBit            = 1 << 7,  // Phase 4C: object was placement-new'd into an OldSmallSpace slab slot
};

// ── Phase 1A: logical heap space a GcObject currently resides in ───────────
// Mirrors GcOldBit until Phase 5B activates the nursery bump allocator.
// EnvArena / CoroArena are reserved for Phases 3B/3C dedicated arenas.
enum class GcSpaceKind : std::uint8_t {
    Nursery       = 0,    // young / not-yet-promoted
    OldSmall      = 1,    // old, small (<= large_object_threshold)
    LargeObject   = 2,    // old, large (>= large_object_threshold, page-aligned)
    Pinned        = 3,    // permanent roots, never swept
    EnvArena      = 4,    // reserved: dedicated Environment arena (Phase 3B)
    CoroArena     = 5,    // reserved: dedicated Coroutine arena  (Phase 3C)
    Uninitialized = 0xFF, // sentinel: space_kind was never explicitly set.
                          // GcHeader defaults to this; any insert() that sees it
                          // means the allocation path forgot to set space_kind.
};
// ──────────────────────────────────────────────────────────────────────────

constexpr std::size_t kGcValueCardSpan = 16;   // values per card granule
constexpr std::size_t kGcCardsPerWord  = 64;   // Phase 3.3: cards packed per uint64_t word

// Number of card granules needed for value_count elements.
inline std::size_t card_count_for_elements(std::size_t value_count) {
    return value_count == 0 ? 0 : ((value_count - 1) / kGcValueCardSpan) + 1;
}

// Phase 3.3: Number of uint64_t bitmap words needed (1 bit per card granule).
inline std::size_t value_card_count(std::size_t value_count) {
    const std::size_t cards = card_count_for_elements(value_count);
    return cards == 0 ? 0 : ((cards - 1) / kGcCardsPerWord) + 1;
}

// Default GC tuning thresholds (used as initial values in Runtime member fields).
constexpr std::size_t kDefaultIncrementalTriggerBytes  = 64 * 1024;   // start incremental marking
constexpr std::size_t kDefaultNurseryTriggerBytes       = 32 * 1024;   // trigger young collection
constexpr std::size_t kDefaultLargeObjectThresholdBytes =  4 * 1024;   // route to LOS above this

// Phase 3.3: Mark the card covering `elem_index` as dirty in the bitmap.
// `cards` is a vector<uint64_t> where each bit represents one card granule.
inline void set_remembered_card(std::vector<std::uint64_t>& cards, std::size_t element_count, std::size_t elem_index) {
    const std::size_t total_cards = card_count_for_elements(element_count);
    const std::size_t word_count  = value_card_count(element_count);
    cards.resize(word_count, 0);
    if (!cards.empty() && total_cards > 0) {
        const std::size_t card_index = std::min(elem_index / kGcValueCardSpan, total_cards - 1);
        cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
    }
}

// Phase 3.3: Iterate over all dirty (set) card indices in a bitmap and call fn(card_index).
// Uses std::countr_zero (C++20) for O(1) lowest-set-bit finding.
template <typename Fn>
void for_each_dirty_card(const std::vector<std::uint64_t>& cards, Fn&& fn) {
    for (std::size_t wi = 0; wi < cards.size(); ++wi) {
        std::uint64_t word = cards[wi];
        while (word != 0) {
            const int bit = std::countr_zero(word);
            fn(wi * kGcCardsPerWord + static_cast<std::size_t>(bit));
            word &= word - 1;  // clear lowest set bit
        }
    }
}

struct GcHeader {
    std::uint32_t size_bytes  = 0;
    std::uint16_t type_id     = 0;
    GcColor       color       = GcColor::White;
    std::uint8_t  flags       = 0;
    std::uint8_t  age         = 0;
    // Phase 1A fields — packed into the 7B padding between age and next_all.
    // Expected layout (MSVC/Clang/GCC x64):
    //   offset 9:  space_kind  (1B)
    //   offset 10: size_class  (1B)
    //   offset 11: [5B padding remains]
    GcSpaceKind   space_kind  = GcSpaceKind::Uninitialized;  // must be set before insert()
    std::uint8_t  size_class  = 0;                       // future: OldSmallSpace size-class index
    GcObject*     next_all    = nullptr;
    GcObject*     next_gray   = nullptr;
};

// ── Phase 1A layout guards ─────────────────────────────────────────────────
// Layout after Phase 1A (space_kind + size_class added into ex-padding):
//   offset  0: uint32_t   size_bytes  (4B)
//   offset  4: uint16_t   type_id     (2B)
//   offset  6: GcColor    color       (1B)
//   offset  7: uint8_t    flags       (1B)
//   offset  8: uint8_t    age         (1B)
//   offset  9: GcSpaceKind space_kind (1B)  ← Phase 1A
//   offset 10: uint8_t    size_class  (1B)  ← Phase 1A
//   offset 11: [5B padding]
//   offset 16: GcObject*  next_all    (8B)
//   offset 24: GcObject*  next_gray   (8B)
//   sizeof = 32B                            (unchanged)
//
// After Phase 6 (remove next_all): sizeof drops to 24B.
// Update the assertion below after each intentional modification.
static_assert(sizeof(GcHeader) == 32,
    "GcHeader size changed. Update after intentional Phase 1A/6 modification.");
static_assert(offsetof(GcHeader, next_all) == 16,
    "GcHeader::next_all offset changed. Verify padding assumptions.");
static_assert(offsetof(GcHeader, next_gray) == 24,
    "GcHeader::next_gray offset changed.");
static_assert(alignof(GcHeader) == alignof(GcObject*),
    "GcHeader alignment must equal pointer alignment (8B on x64).");
// ──────────────────────────────────────────────────────────────────────────

struct GcObject {
    explicit GcObject(ObjectKind kind) : kind(kind) {
#ifdef DEBUG_LEAK_CHECK
        on_gc_object_created();
#endif
        header.type_id = static_cast<std::uint16_t>(kind);
    }
    virtual ~GcObject() {
#ifdef DEBUG_LEAK_CHECK
        on_gc_object_destroyed();
#endif
    }

    // Phase 7 (compaction): move constructor for relocating objects.
    // Base class copies `kind` and `header` (both trivially copyable).
    // Derived classes get implicit move constructors that move their own
    // members (std::string, std::vector, etc.) — no manual overrides needed
    // because no derived class declares a user-defined destructor, copy, or
    // move special member function.
    GcObject(GcObject&& other) noexcept : kind(other.kind), header(other.header) {
#ifdef DEBUG_LEAK_CHECK
        on_gc_object_created();  // balance the old object's future on_gc_object_destroyed
#endif
    }
    GcObject(const GcObject&) = delete;
    GcObject& operator=(const GcObject&) = delete;
    GcObject& operator=(GcObject&&) = delete;

    ObjectKind kind;
    GcHeader header;

    virtual void trace(class Runtime& runtime) = 0;

    // Phase 7 (compaction) hooks — no-op defaults; override in movable subclasses.
    //
    // is_movable(): returns true if the GC may relocate this object to a new
    // address during a compaction pass. Defaults to false; subclasses that hold
    // no raw self-references (e.g. StringObject, ArrayObject) can override.
    // Objects with internal raw pointers (Environment, CoroutineObject) must
    // stay false until Phase 7 introduces handle-indirection.
    virtual bool is_movable() const noexcept { return false; }

    // update_internal_pointers(): called by the compactor after moving `this`
    // to `new_self` so that any internal raw self-pointers can be fixed up.
    // Default is a no-op; only objects with self-referential fields need to
    // override.  The compactor calls this BEFORE updating external references.
    virtual void update_internal_pointers(
        [[maybe_unused]] GcObject* new_self) noexcept {}
};

// Phase 7 (compaction): Value stores raw GcObject* pointers.  Instead of
// handle indirection, Phase 7 uses forwarding-pointer compaction: after
// relocating an object, a forwarding marker (GcColor::Gray at Idle phase) is
// left at the old address and a full-heap reference-fixup pass updates every
// GcObject*/Value in the object graph.
struct Value {
    static constexpr std::uint64_t kPayloadMask = 0x0000FFFFFFFFFFFFull;
    static constexpr std::uint64_t kTagMask = 0xFFFF000000000000ull;
    static constexpr std::uint64_t kHostHandleTag = 0xFFF8000000000000ull;
    static constexpr std::uint64_t kNilTag = 0xFFFA000000000000ull;
    static constexpr std::uint64_t kBoolTag = 0xFFFB000000000000ull;
    static constexpr std::uint64_t kPointerTag = 0xFFFC000000000000ull;
    static constexpr std::uint64_t kHostHandleSlotMask = 0x0000FFFF00000000ull;
    static constexpr std::uint64_t kIntTag = 0xFFFE000000000000ull;
    static constexpr std::uint64_t kCanonicalQuietNaN = 0x7FF8000000000000ull;
    static constexpr std::int64_t kIntMin = -(std::int64_t(1) << 47);
    static constexpr std::int64_t kIntMax = (std::int64_t(1) << 47) - 1;

    static constexpr bool int_fits(std::int64_t value) noexcept {
        return value >= kIntMin && value <= kIntMax;
    }

    static Value nil() { return Value{}; }

    static Value boolean(bool value) {
        Value out;
        out.bits_ = kBoolTag | (value ? 1ull : 0ull);
        return out;
    }

    static Value integer(std::int64_t value) {
        if (!int_fits(value)) {
            fail("Integer value out of 48-bit NaN-boxing range.");
        }
        Value out;
        out.bits_ = kIntTag | (static_cast<std::uint64_t>(value) & kPayloadMask);
        return out;
    }

    static Value from_double(double value) {
        Value out;
        out.bits_ = canonicalize_double_bits(value);
        return out;
    }

    static Value floating(double value) {
        return from_double(value);
    }

    static Value object(GcObject* object) {
        const auto ptr = reinterpret_cast<std::uintptr_t>(object);
        if ((ptr & ~static_cast<std::uintptr_t>(kPayloadMask)) != 0) {
            fail("Object pointer exceeds 48-bit NaN-boxing payload.");
        }
        Value out;
        out.bits_ = kPointerTag | static_cast<std::uint64_t>(ptr);
        return out;
    }

    static Value host_handle(HostHandleToken handle) {
        if ((handle.slot & 0xFFFF0000u) != 0) {
            fail("Host handle slot exceeds NaN-boxing payload.");
        }
        Value out;
        out.bits_ = kHostHandleTag |
                    (static_cast<std::uint64_t>(handle.slot) << 32) |
                    static_cast<std::uint64_t>(handle.generation);
        return out;
    }

    Value() = default;
    explicit Value(bool value) : Value(boolean(value)) {}
    explicit Value(std::int64_t value) : Value(integer(value)) {}
    explicit Value(double value) : Value(from_double(value)) {}
    explicit Value(GcObject* value) : Value(object(value)) {}
    explicit Value(HostHandleToken value) : Value(host_handle(value)) {}

    bool is_nil() const { return bits_ == kNilTag; }
    bool is_bool() const { return (bits_ & kTagMask) == kBoolTag; }
    bool is_int() const { return (bits_ & kTagMask) == kIntTag; }
    bool is_double() const { return !is_boxed(); }
    bool is_float() const { return is_double(); }
    bool is_number() const { return is_int() || is_double(); }
    bool is_object() const { return has_pointer_tag() && (bits_ & kPayloadMask) != 0; }
    bool is_host_handle() const { return (bits_ & kTagMask) == kHostHandleTag && as_host_handle().valid(); }
    bool is_string() const { return is_object() && as_object()->kind == ObjectKind::String; }

    bool as_bool() const { return (bits_ & 1ull) != 0; }

    std::int64_t as_int() const {
        std::uint64_t payload = bits_ & kPayloadMask;
        if ((payload & (1ull << 47)) != 0) {
            payload |= ~kPayloadMask;
        }
        return static_cast<std::int64_t>(payload);
    }

    double as_double() const {
        return is_int() ? static_cast<double>(as_int()) : std::bit_cast<double>(bits_);
    }

    double as_float() const { return as_double(); }

    GcObject* as_object() const {
        if (!has_pointer_tag()) {
            return nullptr;
        }
        return reinterpret_cast<GcObject*>(static_cast<std::uintptr_t>(bits_ & kPayloadMask));
    }

    HostHandleToken as_host_handle() const {
        HostHandleToken token;
        token.slot = static_cast<std::uint32_t>((bits_ & kHostHandleSlotMask) >> 32);
        token.generation = static_cast<std::uint32_t>(bits_ & 0xFFFFFFFFull);
        return token;
    }

private:
    static std::uint64_t canonicalize_double_bits(double value) {
        if (std::isnan(value)) {
            return kCanonicalQuietNaN;
        }
        return std::bit_cast<std::uint64_t>(value);
    }

    bool has_pointer_tag() const {
        return (bits_ & kTagMask) == kPointerTag;
    }

    bool is_boxed() const {
        const std::uint64_t tag = bits_ & kTagMask;
        return tag == kHostHandleTag || tag == kNilTag || tag == kBoolTag || tag == kPointerTag || tag == kIntTag;
    }

    std::uint64_t bits_ = kNilTag;
};

static_assert(sizeof(Value) == 8, "Value must remain an 8-byte NaN-boxed payload.");

// Phase 7 (compaction): resolve a GcObject* that may have been forwarded.
// During compaction, a moved object's old location has color == Gray (sentinel
// at Idle phase where all live objects should be White) and next_gray holds
// the new address.  This function follows one level of forwarding; compaction
// never chains multiple forwards.
inline GcObject* gc_resolve_forwarding(GcObject* obj) noexcept {
    if (obj != nullptr && obj->header.color == GcColor::Gray)
        return obj->header.next_gray;
    return obj;
}

// Phase 7: update the GcObject* inside a Value if it was forwarded.
inline void gc_fixup_value(Value& v) noexcept {
    if (GcObject* object = v.as_object(); object != nullptr) {
        v = Value::object(gc_resolve_forwarding(object));
    }
}

RuntimeResult<double> numeric_value(const Value& value, const std::string& module_name, const Span& span);

struct UpvalueCellObject;

struct Binding {
    Value value;
    UpvalueCellObject* cell = nullptr;
    bool mutable_value = false;
    std::optional<std::string> type_name;
};

enum class EnvironmentKind : std::uint8_t {
    Root,
    Module,
    Local,
};

struct Environment final : GcObject {
    explicit Environment(Environment* parent, EnvironmentKind kind = EnvironmentKind::Local)
        : GcObject(ObjectKind::Environment), parent(parent), kind(kind) {}
    void trace(class Runtime& runtime) override;

    Environment* parent = nullptr;
    EnvironmentKind kind = EnvironmentKind::Local;
    std::uint64_t version = 0;
    std::vector<std::string> binding_names;
    std::unordered_map<std::string, std::size_t> binding_slots;
    std::vector<std::uint64_t> remembered_cards;  // Phase 3.3: bitmap (1 bit = 1 card granule)
    std::unordered_map<std::string, Binding> values;

    void collect_names(std::vector<std::string>& out) const {
        for (const auto& [name, _] : values) {
            out.push_back(name);
        }
        if (parent) parent->collect_names(out);
    }
};

struct UpvalueCellObject final : GcObject {
    explicit UpvalueCellObject(Value value,
                               bool mutable_value = false,
                               std::optional<std::string> type_name = std::nullopt,
                               HandleContainerKind container_kind = HandleContainerKind::ClosureCapture)
        : GcObject(ObjectKind::UpvalueCell),
          value(value),
          mutable_value(mutable_value),
          type_name(std::move(type_name)),
          container_kind(container_kind) {}
    void trace(class Runtime& runtime) override;

    Value value = Value::nil();
    bool mutable_value = false;
    std::optional<std::string> type_name;
    HandleContainerKind container_kind = HandleContainerKind::ClosureCapture;
};

inline Value read_binding_value(const Binding& binding) {
    return binding.cell != nullptr ? binding.cell->value : binding.value;
}

inline void write_binding_value(Binding& binding, Value value) {
    binding.value = value;
    if (binding.cell != nullptr) {
        binding.cell->value = value;
    }
}

struct StringObject final : GcObject {
    explicit StringObject(std::string value) : GcObject(ObjectKind::String), value(std::move(value)) {}
    void trace(class Runtime&) override {}
    std::string value;
    bool is_interned = false;
};

struct ArrayObject final : GcObject {
    ArrayObject() : GcObject(ObjectKind::Array) {}
    void trace(class Runtime& runtime) override;
    std::vector<Value> elements;
    std::vector<std::uint64_t> remembered_cards;  // Phase 3.3: bitmap (1 bit = 1 card granule)
};

using BytecodeConstant = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
