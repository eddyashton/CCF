# Initial Evaluation of Micro QuickJS for CCF

Date: 11 September 2026

## Executive summary

Micro QuickJS is not currently a viable transparent replacement for QuickJS
in CCF.

The JavaScript subset is already a hard compatibility break for current CCF
applications and constitutions. The C API is only superficially similar to
QuickJS, and CCF relies on several QuickJS-specific facilities that Micro
QuickJS does not provide. Its FFI and garbage-collection model removes some
reference-counting hazards, but introduces different rooting and lifecycle
hazards, including unresolved upstream finalizer issues.

A small compile-time prototype may still be worthwhile if the intended result
is an explicitly restricted v2 application profile rather than compatibility
with existing CCF JavaScript. Micro QuickJS's architecture plausibly provides
cheaper bare-context creation, a smaller memory footprint, and cheaper
installation of stable native APIs through generated ROM tables. Upstream does
not publish startup timings or a controlled comparison with QuickJS, however,
so a noticeable end-to-end improvement for CCF remains unproven.

The recommended next step, if this investigation continues, is a bounded
compile-time prototype. It should separately measure engine initialization,
CCF FFI initialization, source parsing, first execution, and cached execution,
and it should have explicit compatibility and FFI stop conditions.

## Scope and methodology

This investigation considered four questions:

1. Whether Micro QuickJS supports the JavaScript used by CCF today.
2. Whether it is likely to improve CCF's startup-dominated JavaScript
   workloads.
3. Whether it can replace QuickJS behind the existing CCF wrappers, or be
   offered as an alternative backend.
4. Whether its extension and FFI interface is simpler or safer than the current
   QuickJS integration.

The compatibility audit covered 73 CCF-owned JavaScript and TypeScript files,
including constitutions, samples, test applications, and the CCF JavaScript
application library. Generated output, third-party packages, and
`node_modules` were excluded. Raw JavaScript executed directly by the engine
was distinguished from TypeScript that is currently emitted with an ES2020
target.

The integration audit traced CCF's runtime and context wrappers, value
ownership, module loading and bytecode caching, native extensions, KV handle
objects, runtime limits, and interpreter reuse. The current integration uses
113 distinct QuickJS API symbols across 31 files, covering approximately
10,000 lines including tests.

The upstream assessment is pinned to Micro QuickJS commit
[`203d5bb79789bc47b74855d9207415dab71661a0`](https://github.com/bellard/mquickjs/commit/203d5bb79789bc47b74855d9207415dab71661a0),
dated 4 June 2026. Micro QuickJS currently has no release tags or stated
compatibility policy, so any prototype should pin a complete commit hash.

## 1. JavaScript compatibility

### Micro QuickJS's language profile

Micro QuickJS implements a strict, mostly ES5 subset. Its supported
post-ES5 additions include typed arrays, exponentiation, `globalThis`, a
limited `for...of`, selected newer `Math` and `String` methods, and selected
regular-expression flags.

Important unsupported constructs include:

- ECMAScript modules (`import` and `export`)
- `class`
- `let` and `const`
- arrow functions
- template literals
- destructuring
- spread and rest syntax
- generators and `yield`
- `async` and `await`
- promises and the job queue
- `Map`, `Set`, and weak collections
- `Proxy` and `Reflect`
- `Symbol`
- `BigInt`
- direct, local `eval`

It also has observably different semantics in several areas:

- Sparse array literals are rejected, and arrays cannot be extended past their
  end except at exactly `length`.
- Properties have simplified writable, enumerable, and configurable
  semantics.
- `for...in` only visits own properties.
- `for...of` is limited to arrays and does not support general iterators.
- Some string and regular-expression Unicode behavior is ASCII-only or differs
  from standard UTF-16 behavior.
- Primitive boxing such as `new Number(1)` is unsupported.

Primary upstream references:

- [JavaScript subset and stricter mode](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/README.md#L67-L190)
- [Parser tokens and reserved keywords](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/mquickjs.c#L7160-L7245)
- [Shipped standard-library objects](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/mqjs_stdlib.c#L334-L389)

### Definite CCF incompatibilities

The restrictions are already breaking issues, rather than hypothetical
limitations for future applications.

#### Modules

Thirty-five of the 44 audited raw JavaScript files use `import` or `export`.
This includes the default constitution:

- [resolve.js](samples/constitutions/default/resolve.js)
- [validate.js](samples/constitutions/default/validate.js)
- [apply.js](samples/constitutions/default/apply.js)

Micro QuickJS has no ECMAScript module parser, module runtime, module namespace,
or loader API. Its CLI `load()` function evaluates a file as a global script;
it is not a module loader.

CCF would need to bundle each application or constitution into a global script,
or define and maintain a separate host module convention. This would also
change how CCF compiles modules, resolves dependencies, extracts exported
handlers, and caches bytecode.

#### Modern syntax in constitutions and applications

The default constitution's
[actions.js](samples/constitutions/default/actions.js) starts with a class and
contains extensive use of `const`, `let`, template literals, arrow functions,
and destructuring.

These constructs are also present across other constitutions, sample
applications, and tests. Representative examples include:

- [role definition actions](samples/constitutions/roles/set_role_definition.js)
- [logging sample](samples/apps/logging/js/src/logging.js)
- [batched sample](src/apps/batched/src/batched.js)
- [dynamic module test](tests/js-modules/dynamic-module-import/src/test_module.js)

The dynamic module test additionally uses `async`, `await`, and dynamic import.

#### Runtime facilities

Current tests and libraries also depend on missing runtime features:

- [JavaScript limit tests](tests/js-limits/src/limits.js) use direct `eval` and
  `Proxy`.
- [CCF converters](js/ccf-app/src/converters.ts) use native `BigInt`, including
  64-bit integer conversion and `DataView` BigInt accessors.
- [Interpreter-reuse tests](tests/js-interpreter-reuse/src/singleton_service_registry.ts)
  use `Map`.

`BigInt` is particularly important because this is not merely unsupported
syntax. Preserving the current 64-bit conversion semantics would require a
different representation and API contract or a substantial polyfill.

#### Current TypeScript output

The CCF application library currently targets ES2020 in
[js/ccf-app/tsconfig.json](js/ccf-app/tsconfig.json). The generated application
code therefore retains constructs such as classes, `let`, `const`, arrow
functions, and other unsupported features.

A new transpilation and bundling pipeline could remove some syntax and flatten
modules. It would not automatically provide missing runtime objects, preserve
all observable semantics, or make existing and member-supplied constitutions
compatible.

### Compatibility conclusion

The current CCF JavaScript contract cannot run on Micro QuickJS unchanged.

Supporting Micro QuickJS would require a deliberately narrower application
profile with:

- mandatory bundling into global scripts;
- aggressive downlevel transpilation;
- restrictions or replacements for unsupported runtime facilities;
- explicit handling of `BigInt` and 64-bit values;
- documented semantic differences;
- separate compatibility tests and likely separate application artifacts.

This is a new JavaScript profile, not an implementation detail hidden beneath
the existing CCF JavaScript API.

## 2. Performance prospects

### What upstream supports

Micro QuickJS is designed for small embedded systems. Upstream claims:

- operation with approximately 10 kB RAM;
- roughly 100 kB ROM on ARM Thumb-2, including the C library;
- execution speed comparable to QuickJS;
- very low context-instantiation cost because much of the standard library and
  its properties remain in ROM.

See the upstream [overview and size claims](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/README.md#L6-L43)
and [implementation notes](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/README.md#L286-L349).

These claims make a lower bare-context cost plausible. Micro QuickJS:

- creates a context in a caller-provided fixed arena;
- has no separate runtime object;
- stores values in one machine word;
- keeps much of its standard library in generated static tables;
- parses directly to bytecode without building an AST;
- uses a shared arena for the context, heap, classes, and VM stack.

### What upstream does not demonstrate

Upstream provides no quantitative startup benchmark or controlled QuickJS
comparison. It has:

- no checked-in result table;
- no checked-in QuickJS baseline for its microbenchmark;
- no variance or confidence intervals;
- no decomposition of context creation, library initialization, parsing, and
  first execution;
- no CCF-like native-call or ArrayBuffer-heavy workload.

The included microbenchmark largely measures steady-state execution and reports
the minimum observed result. The patched Octane suite is not representative of
CCF's startup-dominated workload.

Relevant sources:

- [Microbenchmark harness](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/tests/microbench.js#L84-L153)
- [Missing optional baseline path](https://github.com/bellard/mquickjs/blob/203d5bb79789bc47b74855d9207415dab71661a0/tests/microbench.js#L999-L1132)

### CCF-specific startup costs

A fresh CCF interpreter currently:

1. Creates a QuickJS runtime and context in
   [runtime.cpp](src/js/core/runtime.cpp) and
   [context.cpp](src/js/core/context.cpp).
2. Registers native classes, including KV and historical classes.
3. Installs common extensions through
   [common_context.h](include/ccf/js/common_context.h).
4. Installs invocation-specific KV, RPC, and request extensions in
   [registry.cpp](src/js/registry.cpp).
5. Loads or deserializes modules and locates exported handlers.

The CCF integration contains 63 native callback definitions and 61 explicit
`new_c_function` registration sites, in addition to dynamically created KV
methods. This supports the concern that native API construction can be a
material part of cold startup.

Micro QuickJS's generated ROM-resident library tables could remove much of the
repeated construction of stable native API shapes. This may be the most
promising startup optimization. Request-specific objects and native state would
still need to be created or attached for every invocation.

CCF already amortizes initialization through interpreter reuse in
[interpreter_cache.h](src/js/interpreter_cache.h). Stable extensions are
installed when a cached interpreter is created, while request-specific
extensions are added for each call. Any performance comparison must therefore
measure both uncached and cached requests.

### Performance conclusion

- A lower bare-context creation cost is likely.
- A lower memory footprint is likely.
- A noticeable cold-request improvement is plausible, particularly if static
  binding tables replace repeated extension construction.
- A noticeable overall CCF improvement is unknown.
- No responsible numeric estimate can be made from upstream evidence.

The primary uncertainty is exactly the one motivating this investigation:
whether CCF's own FFI and request setup dominates enough of startup that the
smaller engine does not materially improve end-to-end latency.

## 3. API and integration similarity

### Superficial similarities

Both APIs use familiar names and concepts, including `JSContext`, `JSValue`,
`JS_Eval`, object and array construction, property access, exception sentinels,
and `JS_Throw*` functions.

The similarity ends at the architectural boundary.

### Important incompatibilities

#### Runtime and memory model

QuickJS has a `JSRuntime` and one or more `JSContext` objects. Micro QuickJS
constructs a single context directly in a caller-owned fixed arena and has no
public runtime object.

CCF currently changes heap and stack limits on reused runtimes in
[runtime.cpp](src/js/core/runtime.cpp). Micro QuickJS fixes the joint
heap/stack arena size before context construction, so it cannot directly
reproduce this behavior.

#### Value ownership

QuickJS uses reference counting. CCF's central
[JSWrappedValue](include/ccf/js/core/wrapped_value.h) RAII type is built around
`JS_DupValue` and `JS_FreeValue`, implemented in
[wrapped_value.cpp](src/js/core/wrapped_value.cpp).

Micro QuickJS uses a moving tracing collector and has no equivalent
duplicate/free calls. Values retained across allocating API calls must instead
be placed in explicit GC roots. The existing value wrapper cannot be preserved
by changing includes or aliases.

#### Native callback ABI and calls into JavaScript

Micro QuickJS native callbacks have a different argument ABI. Calling
JavaScript from C requires explicit VM-stack capacity checks and pushing the
arguments, function, and receiver before `JS_Call`.

CCF currently builds an argument array and directly calls `JS_Call` in
[Context::inner_call](src/js/core/context.cpp).

#### Modules and bytecode

CCF relies on:

- module compilation and resolution;
- module loader callbacks;
- module namespace extraction;
- promise state when evaluating modules;
- QuickJS bytecode serialization and deserialization;
- versioned bytecode stored in replicated KV tables.

These paths are implemented in:

- [context.cpp](src/js/core/context.cpp)
- [kv_module_loader.h](src/js/modules/kv_module_loader.h)
- [kv_bytecode_module_loader.h](src/js/modules/kv_bytecode_module_loader.h)
- [registry.cpp](src/js/registry.cpp)

Micro QuickJS has no ECMAScript module API. It can parse and run bytecode
separately and supports persistent bytecode, but its bytecode is
word-size- and endian-dependent, must be relocated, has no backward
compatibility guarantee, is not validated, and requires its backing buffer to
remain alive.

#### Native classes and dynamic objects

CCF uses QuickJS class definitions, exotic property methods, opaque native
state, and finalizers. Dynamic KV lookup is implemented with exotic methods in
[kv.cpp](src/js/extensions/ccf/kv.cpp), and KV handles transfer native state to
JavaScript objects in [kv_helpers.h](src/js/extensions/ccf/kv_helpers.h).

Micro QuickJS supports opaque user objects and finalizers, but native classes
and functions are primarily generated into static tables at build time. Runtime
function creation selects a previously generated function-table index rather
than accepting an arbitrary C function pointer.

#### Typed arrays and buffers

Typed arrays exist in Micro QuickJS JavaScript, but its public native API has no
equivalent to QuickJS's typed-array buffer accessors. CCF's converters,
cryptography, request body, attestation, KV, and response paths are heavily
ArrayBuffer-oriented, so this gap requires investigation or an upstream/API
extension.

### Replacement options

| Model                                                | Assessment                                                                                                                                                                      |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Drop-in implementation beneath the existing wrappers | Not feasible. Existing wrappers expose QuickJS types, ownership, modules, tags, and class mechanisms directly.                                                                  |
| Compile-time alternative backend                     | The most feasible experiment. Preserve the user-facing CCF API where possible, but provide engine-specific context, value, module/bundle, extension, and limit implementations. |
| Runtime-selectable backend                           | Possible only after a major engine-neutral abstraction effort. Both libraries, value models, application artifacts, bytecode formats, and test profiles would need to coexist.  |
| Per-endpoint runtime selection                       | Not recommended initially. It adds profile negotiation, cache and bytecode complexity, operational ambiguity, and replicated configuration concerns.                            |

If an alternative engine is offered, engine selection must be explicit and
consistent across nodes. Silent fallback would be unsafe because the engines
accept different programs and have observably different semantics.

## 4. FFI and extension ergonomics

### Potential simplifications

Micro QuickJS offers some attractive properties:

- Values do not require a manual duplicate/free operation at every ownership
  boundary.
- String conversion returns a borrowed pointer and does not require a separate
  free operation.
- A fixed arena provides a simple deterministic top-level memory bound.
- Stable functions, classes, and properties can be generated into ROM tables
  rather than dynamically recreated for every context.
- A nonzero interrupt callback produces an uncatchable internal interruption,
  which is compatible in spirit with CCF's execution-time enforcement.

### New awkwardness and risks

#### Static registration model

CCF's extensions dynamically populate objects through
[ExtensionInterface::install](include/ccf/js/extensions/extension_interface.h).
Micro QuickJS's intended model generates the callable function table and much
of the object layout at build time.

This may improve startup, but it is not a direct fit. Stable API shape could be
generated statically, while per-request state is reached through the context
opaque pointer or native object state. This would require redesigning the
extension boundary.

#### Moving-GC rooting discipline

Allocation can move objects. Any value retained across a potentially allocating
API call must be rooted explicitly with `JSGCRef`. This removes QuickJS's
reference-count discipline but replaces it with a rooting discipline that can
produce dangling values or leaks if applied incorrectly.

String pointers are also borrowed and can be invalidated by a later allocating
call. The recent CCF fixes for ArrayBuffer and string lifetimes demonstrate
that this class of re-entrancy and lifetime issue is directly relevant.

#### Manual VM-stack calls

Calling JavaScript from C requires explicit stack-capacity checks and ordered
pushes. This is less idiomatic for CCF than the current direct call interface
and creates another invariant for wrappers to enforce.

#### Native lifecycle defects

At the pinned upstream commit, finalizer behavior has unresolved defects:

- [issue #79](https://github.com/bellard/mquickjs/issues/79)
- [issue #81](https://github.com/bellard/mquickjs/issues/81)
- unmerged [PR #71](https://github.com/bellard/mquickjs/pull/71)

The reported sweep/coalescing behavior can skip finalizers for consecutive
native objects. For CCF this could leak native memory or resources and should
be considered a release blocker unless patched and covered by regression
tests.

Micro QuickJS also lacks a general user-class GC mark callback. As described in
[issue #80](https://github.com/bellard/mquickjs/issues/80), a native object
that retains a JavaScript value through a global root can create an
uncollectable root cycle.

### FFI conclusion

The FFI is different, but not clearly simpler for CCF.

Static native tables and the lack of per-value reference counting are
attractive. However, CCF would trade its current ownership hazards for explicit
moving-GC rooting, borrowed-pointer lifetimes, manual VM-stack calls, limited
buffer access, a less dynamic registration model, and currently unresolved
native-finalizer defects.

Given CCF's recent fixes around use-after-free, ArrayBuffer lifetime, exception
propagation, string truncation, leaked values, runtime limits, and extension
teardown, this should not be treated as a risk-reduction migration without a
substantial dedicated test effort.

## Recommended prototype

Do not begin by redirecting the existing wrappers. Instead, use a bounded
compile-time prototype with explicit gates.

### Gate 1: language and packaging

Bundle and transpile one small application and a reduced constitution into
single strict-subset scripts.

Stop if required semantics cannot be preserved cleanly, especially:

- `BigInt` and 64-bit values;
- module and exported-handler behavior;
- governance constitution extensibility;
- property and iteration semantics;
- application error behavior.

### Gate 2: representative FFI

Generate a ROM library containing:

- representative `ccf` functions;
- string, JSON, and ArrayBuffer conversion;
- one KV-handle analogue with native state;
- a C-to-JavaScript callback;
- exception propagation;
- deadline interruption;
- native finalization.

Patch and regression-test the known finalizer issue before relying on opaque
native resources.

### Gate 3: decomposed benchmark

Build pinned QuickJS and Micro QuickJS configurations with comparable compiler
settings and measure:

1. Bare engine/context creation.
2. Context creation with the CCF-equivalent native API surface.
3. Source parsing and compilation.
4. First execution after parsing.
5. Precompiled-bytecode relocation/loading and execution.
6. First uncached CCF-style request.
7. Cached/reused CCF-style request.
8. JSON-, property-, ArrayBuffer-, and native-call-heavy handlers.
9. Peak memory, minimum successful arena size, GC frequency, and GC pause time.

Report p50, p95, and p99 rather than only best-case results. Separate engine
initialization from FFI installation so that the optimization target is
identified directly.

### Gate 4: safety and compatibility

Before considering production use, require:

- regression tests for GC rooting and all native finalizers;
- malformed input and out-of-memory tests at each FFI boundary;
- execution deadline and uncatchable interruption tests;
- equivalent CCF authorization and KV access behavior;
- deterministic engine selection across nodes;
- an explicit application profile and compatibility/versioning policy;
- pinned source and bytecode compatibility handling.

## Final assessment

| Axis                             | Initial assessment                                                                 |
| -------------------------------- | ---------------------------------------------------------------------------------- |
| Current JavaScript compatibility | Fails. Unsupported syntax, modules, and runtime facilities are pervasive.          |
| Bare startup potential           | Promising but unquantified.                                                        |
| End-to-end CCF performance       | Unknown; FFI and request setup may dominate.                                       |
| Drop-in API compatibility        | Low; not a header or implementation substitution.                                  |
| Compile-time v2 backend          | Feasible as an experiment with a restricted application profile.                   |
| Runtime/per-endpoint selection   | High complexity and not recommended initially.                                     |
| FFI ergonomics                   | Some static/startup advantages, but different and substantial lifetime complexity. |
| Production maturity for CCF      | Insufficient at the pinned commit due to API gaps and unresolved finalizer issues. |

The most defensible path is therefore:

1. Keep QuickJS as the supported engine.
2. Prototype Micro QuickJS only as an explicitly restricted compile-time
   alternative.
3. Use the prototype to measure CCF's actual startup decomposition.
4. Continue only if it demonstrates a material end-to-end improvement and the
   compatibility and native-lifecycle gaps can be addressed without creating a
   second costly JavaScript platform.
