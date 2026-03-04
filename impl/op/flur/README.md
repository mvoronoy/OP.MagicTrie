# Flur library

## Functional lambda 

Flur is intended to give C++ developers build functional lazy evaluated pipeline for data 
processing. Project uses features of c++17 standard and inspired by best practices of
.Net lambda fluent interface and Java streams.
Flur allows developer to create processing pipeline in style of functional programming. 
Developers can use as synchronous as
asynchronous processing.

To build Flur pipeline we need specify some source of information, then we can add 
multiple (or even zero) processing declarations and last some consumption 
declaration.

Flur provides some useful implementations for 
- Source
- Processing
- Consumption.

And of course, developers can provide own implementation.

## Getting started
Let's start from simple. Let's evaluate square of each integer in some std::vector:

    std::vector<int> int_vector{1, 2, 3, 4, 5};                     // (1)
    // add lazy avaluation of squared                       
    auto squared = OP::flur::src::of_container(int_vector)          // (2)
        >> OP::flur::then::mapping([](auto n){ return n ** n; })    // (3)
    ;
    //let's print result sequnce
    size_t count = 0;
    for(auto n : squared)                                           // (4)
        std::cout << (count++ ? ", " : "") << n;                    // (5)
    std::cout << "\n";

(2) Most of the flur functions belongs to the namespace `src` or `then`. This line starts 
new lazy evaluation. `src::of_container` explicitly copies `int_vector` to 
introduce abstraction of evaluation processor `LazyRange`. To avoid redundant vector copying
you could use `std::cref` for const-referencing or `std::move` for moving entire vector.
(3) `operator >>` used to combine multiple evaluation steps into single processor. This example
adds evaluation of square by `then::mapping` that accepts lambda to evaluate `n*n`. Pay your 
attention that no real evaluation yet, only declaration.
(4, 5) using regular c++1x iteration syntax we can perform real evaluation and expected output is:

    1, 4, 9, 16, 25

Let's complicate this example by adding more processors to LazyRange. We can continue to play with
existing `squared` and filter-out odd numbers:

    template <typename Lazy>
    auto even_only_range(const Lazy& previous)                      // (6)
    {
        return previous >> OP::flur::then::filter(                  // (7)
            [](int n) -> bool { return 0 == (n & 1); });
    }
    // ...
    count = 0;
    for(auto n : even_only_range(squared))                          // (8)
        std::cout << (count++ ? ", " : "") << n;
    std::cout << "\n";
    
(6) we can continue with previous arbitrary lazy ranges. Since `operator >>` creates new data type 
to avoid complex type evaluation `template` argument is used.
(7) `then::filter` accept lambda that must evaluate boolean. In this example we accept only even 
numbers `0 == (n & 1)`
(8) print new range that now consist of 3 evaluation steps (2), (3) and (7) with result:

    4, 16

As you can see (4, 5) and (8) can be separated to the standalone reusable function:

    template <class Lazy>
    void pretty_print(const Lazy& range)
    {
        size_t count = 0;
        for(auto n : even_only_range(squared))                          
            std::cout << (count++ ? ", " : "") << n << "\n";            
    }

And last step that rounds up the simple scenario, reusing functionality for other sources:

    using namespace OP::flur;
    template <class Lazy>
    auto square_and_even(const Lazy& left)
    {
        return lazy 
            >> then::mapping([](auto n){ return n ** n; }) 
            >> then::filter( 
                [](int n) -> bool { return 0 == (n & 1); })
        ;
    }
    
    // apply algorithm to multiple sources
    pretty_print(square_and_even((srs::of_value(42)))); // iterate exactly 1 
    pretty_print(square_and_even((srs::of_optional(42)))); // iterate 0 or 1 item
    pretty_print(square_and_even((srs::of_iota(1, 42)))); // iterate in range [1..42)

In the functional programming such lazy evaluated constructions that allows you create abstraction over 
arbitrary sources called 'monad'.

## Sources

Flur provides various source factories to create data sequences:

| Source | Description |
|--------|-------------|
| `src::of_container(c)` | Create sequence from STL container |
| `src::of_value(v)` | Single value as sequence |
| `src::of_optional(o)` | Optional value (0 or 1 items) |
| `src::of_iota(b, e)` | Numeric range [b, e) |
| `src::of_iterators(b, e)` | Iterator pair as sequence |
| `src::generator(f)` | Custom generator function |

## Operations (then namespace)

### Mapping
Transform each element:

```cpp
auto result = src::of_container(vec)
    >> then::mapping([](int x) { return x * 2; });
```

### Filtering
Filter elements by predicate:

```cpp
auto result = src::of_container(vec)
    >> then::filter([](int x) { return x > 0; });
```

### Flat Mapping
Flatten nested sequences:

```cpp
auto result = src::of_container(vec)
    >> then::flat_mapping([](int x) {
        return src::of_iota(0, x);
    });
```

### Combining Sequences

#### Union
Combine multiple sequences:

```cpp
auto result = src::of_container(a)
    >> OP::flur::then::union_all(src::of_container(b));
```

#### Zip
Pair elements from multiple sources:

```cpp
auto result = src::of_container(nums)
    >> then::zip(src::of_container(strs));
```

#### Cartesian Product
All combinations of two sequences:

```cpp
auto result = src::of_container({1, 2})
    >> then::cartesian(src::of_container({'a', 'b'}));
    // Produces: (1,'a'), (1,'b'), (2,'a'), (2,'b')
```

## Consumers (apply namespace)

### Sum
Calculate sum of values:

```cpp
int sum = 0;
src::of_container({1, 2, 3}) >>= apply::sum(sum);
```

### Collect
Collect to container:

```cpp
std::vector<int> result;
src::of_container({1, 2, 3}) >>= apply::collect(result);
```

### Count
Count elements:

```cpp
size_t count = src::of_iota(0, 100) >>= apply::count();
```

## Advanced Features

### Joins
Join two ordered sequences:

```cpp
// Ordered join
auto joined = left_seq 
    >> then::ordered_join(right_seq, 
        [](const auto& l, const auto& r) { return l.key == r.key; });
```

### Distinct
Remove duplicates:

```cpp
auto unique = src::of_container({1,2,1,3})
    >> then::distinct();  // Ordered distinct
    // or
    >> then::unordered_distinct();  // Hash-based
```

### Parallel Processing
Process with thread pool:

```cpp
auto result = src::of_container(large_data)
    >> then::minibatch(thread_pool, batch_size)
    >> then::mapping(process_fn);
```

### State-aware Processing
Access iteration state:

```cpp
src::generator([](const SequenceState& state) -> std::optional<int> {
    return state.step().current() < 100 
        ? std::optional<int>{state.step().current()}
        : std::nullopt;
})
>> then::filter([](auto i, const SequenceState& state) { 
    return (i ^ state.step().current()) & 1; 
});
```

## Complete Example

```cpp
#include <op/flur/flur.h>

using namespace OP::flur;

// Complex pipeline example
auto result = src::of_container(getData())
    >> then::mapping(transform)
    >> then::filter(predicate)
    >> then::flat_mapping(expand)
    >> then::distinct()
    >>= apply::collect(output);

std::cout << "Processed " << result.size() << " items\n";
```
