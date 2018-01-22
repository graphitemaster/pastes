#ifndef U_TRAITS_HDR
#define U_TRAITS_HDR

namespace u {

namespace detail {
    /// When implementing SFINAE testers, functions with different return type
    /// sizes are used. The following structure ensures a type size of 2 always.
    struct two { char x[2]; };
}

/// integral_constant<T, T:val>
/// Note: Only provides integral_constant::value
template <typename T, T val>
struct integral_constant {
    static constexpr const T value = val;
};

template <typename T, T val>
constexpr const T integral_constant<T, val>::value;

/// true_type
typedef integral_constant<bool, true> true_type;

/// false_type
typedef integral_constant<bool, false> false_type;

/// enable_if
/// Convenient way to leverage SFINAE to conditionally remove functions from
/// the compilers overload resolution set or provide separate function overloads
/// and/or specializations for different type traits.
///
/// Note: enable_if's fail state can be treated as an incomplete type. However,
/// in the context of overload resolution it's more desirable for it to be
/// a unique empty type.
template <bool, typename T = void>
struct enable_if { };

template <typename T>
struct enable_if<true, T> {
    typedef T type;
};

/// remove_const
/// Strip T of its topmost const qualifier.
template <typename T>
struct remove_const {
    typedef T type;
};

template <typename T>
struct remove_const<const T> {
    typedef T type;
};

/// remove_volatile
/// Strip T of its topmost volatile qualifier.
template <typename T>
struct remove_volatile {
    typedef T type;
};

template <typename T>
struct remove_volatile<volatile T> {
    typedef T type;
};

/// remove_cv
/// Helper to strip T of its topmost cv-qualifiers.
template <typename T>
struct remove_cv {
    typedef typename remove_volatile<typename remove_const<T>::type>::type type;
};

/// remove_reference
template <typename T>
struct remove_reference {
    typedef T type;
};

template <typename T>
struct remove_reference<T&> {
    typedef T type;
};

template <typename T>
struct remove_reference<T&&> {
    typedef T type;
};

/// remove_all_extents
template <typename T>
struct remove_all_extents {
    typedef T type;
};

template <typename T>
struct remove_all_extents<T[]> {
    typedef typename remove_all_extents<T>::type type;
};

template <typename T, size_t E>
struct remove_all_extents<T[E]> {
    typedef typename remove_all_extents<T>::type type;
};

/// is_void
template <typename T>
struct is_void : false_type { };

template <>
struct is_void<void> : true_type { };

/// is_same
template <typename T, typename U>
struct is_same : false_type { };

template <typename T>
struct is_same<T, T> : true_type { };

/// is_array
template <typename T>
struct is_array : false_type { };

template <typename T>
struct is_array<T[]> : true_type { };

template <typename T, size_t E>
struct is_array<T[E]> : true_type { };

/// is_reference
template <typename T>
struct is_reference : false_type { };

template <typename T>
struct is_reference<T&> : true_type { };

template <typename T>
struct is_reference<T&&> : true_type { };

/// is_pointer
/// Checks whether T is a pointer to object or pointer to function.
///
/// Note: The following trait is inaccurate and will yield true for pointers to
/// non-static member objects or data members.
namespace detail {
    template <typename T>
    struct is_pointer : false_type { };

    template <typename T>
    struct is_pointer<T*> : true_type { };
}

template<typename T>
struct is_pointer : detail::is_pointer<typename remove_cv<T>::type> { };

/// is_class
///
/// Note: the following trait is inaccurate and will yield true for union types.
/// There is no known way to do "is_union" without support from the compiler.
namespace detail {
    template <typename T>
    class is_class_test {
        template <typename U>
        static char test(int U::*);

        template <typename U>
        static two test(...);
    public:
        static const bool value = sizeof(test<T>(0)) == 1;
    };
}

template <typename T>
struct is_class : integral_constant<bool, detail::is_class_test<T>::value> { };

/// is_function
/// Checks whether T is a function type. Provides member `value' which yields true
/// if T is a function type. Otherwise it yields false.
///
/// Note: the following trait is inaccurate and will yield true for union types.
/// There is no known way to do "is_union" without support from the compiler.
namespace detail {
    template <typename T>
    class is_function_test {
        template <typename U>
        static char test(U*);

        template <typename U>
        static two test(...);

        template <typename U>
        static U& source();
    public:
        static const bool value = sizeof(test<T>(source<T>())) == 1;
    };

    template <typename T,
        bool = is_class<T>::value || is_void<T>::value || is_reference<T>::value>
    struct is_function :
        integral_constant<bool, is_function_test<T>::value>
    { };

    template <typename T>
    struct is_function<T, true> : false_type { };
}

template <typename T>
struct is_function : detail::is_function<T> { };

/// is_convertible
/// If an imaginary rvalue of type T1 can be used in the return statement of a
/// function returning T2, that is, if it can be converted to T2 using implicit
/// conversion rules, the member constant `value' will yield true. Otherwise it
/// will yield false.
namespace detail {
    template <typename T1, typename T2>
    class is_convertible_test {
        template <typename U>
        static char test(U);

        template <typename U>
        static two test(...);

        template <typename U>
        static U &&source();
    public:
        static const bool value = sizeof(test<T2>(source<T1>())) == 1;
    };

    template <typename T, bool = is_array<T>::value,
                          bool = is_function<T>::value,
                          bool = is_void<T>::value>
    struct is_array_function_or_void { enum { value = 0 }; };

    template <typename T>
    struct is_array_function_or_void<T, true, false, false> { enum { value = 1 }; };
    template <typename T>
    struct is_array_function_or_void<T, false, true, false> { enum { value = 2 }; };
    template <typename T>
    struct is_array_function_or_void<T, false, false, true> { enum { value = 3 }; };

    /// A compile time check for conversion safety
    template <typename T,
        size_t = is_array_function_or_void<typename remove_reference<T>::type>::value>
    struct is_convertible_check
        : integral_constant<size_t, 0>
    { };

    template <typename T>
    struct is_convertible_check<T, 0>
        : integral_constant<size_t, sizeof(T)>
    { };

    template <typename T1, typename T2,
        size_t = is_array_function_or_void<T1>::value,
        size_t = is_array_function_or_void<T2>::value>
    struct allow_convertible
        : integral_constant<bool, is_convertible_test<T1, T2>::value>
    { };

    template <typename T1, typename T2>
    struct allow_convertible<T1, T2, 1, 0> : false_type { };

    /// Conversion through cv-qualification
    template <typename T1>
    struct allow_convertible<T1, const T1&, 1, 0> : true_type { };
    template <typename T1>
    struct allow_convertible<T1, const T1&&, 1, 0> : true_type { };
    template <typename T1>
    struct allow_convertible<T1, volatile T1&&, 1, 0> : true_type { };
    template <typename T1>
    struct allow_convertible<T1, const volatile T1&&, 1, 0> : true_type { };

    /// Conversion through cv-qualification w.r.t pointers and array decay
    template <typename T1, typename T2>
    struct allow_convertible<T1, T2*, 1, 0>
        : integral_constant<bool,
            allow_convertible<typename remove_all_extents<T1>::type*, T2*>::value>
    { };

    template <typename T1, typename T2>
    struct allow_convertible<T1, T2* const, 1, 0>
        : integral_constant<bool, allow_convertible<
            typename remove_all_extents<T1>::type*, T2* const>::value>
    { };

    template <typename T1, typename T2>
    struct allow_convertible<T1, T2* volatile, 1, 0>
        : integral_constant<bool,
            allow_convertible<typename remove_all_extents<T1>::type*, T2* volatile>::value>
    { };

    template <typename T1, typename T2>
    struct allow_convertible<T1, T2* const volatile, 1, 0>
        : integral_constant<bool,
            allow_convertible<typename remove_all_extents<T1>::type*, T2* const volatile>::value>
    { };


    /// Function conversions (function pointers, functors, lambdas)
    template <typename T1, typename T2>
    struct allow_convertible<T1, T2, 2, 0> : false_type { };

    template <typename T1>              struct allow_convertible<T1, T1&&, 2, 0> : true_type { };

    template <typename T1>              struct allow_convertible<T1, T1&, 2, 0> : true_type { };
    template <typename T1>              struct allow_convertible<T1, T1*, 2, 0> : true_type { };
    template <typename T1>              struct allow_convertible<T1, T1* const, 2, 0> : true_type { };
    template <typename T1>              struct allow_convertible<T1, T1* volatile, 2, 0> : true_type { };
    template <typename T1>              struct allow_convertible<T1, T1* const volatile, 2, 0> : true_type { };

    /// Unknown state
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 3, 0> : false_type { };

    /// Prevent conversions to and from array, function or void (array)
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 0, 1> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 1, 1> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 2, 1> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 3, 1> : false_type { };

    /// Prevent conversions to and from array, function or void (function)
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 0, 2> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 1, 2> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 2, 2> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 3, 2> : false_type { };

    /// Prevent conversions to and from array or function (void)
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 0, 3> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 1, 3> : false_type { };
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 2, 3> : false_type { };

    /// This is the only suitable conversion
    template <typename T1, typename T2> struct allow_convertible<T1, T2, 3, 3> : true_type { };

    template <typename T1, typename T2>
    struct is_convertible : allow_convertible<T1, T2> {
        /// Prevent conversions between incomplete types
        static const size_t checkT1 = is_convertible_check<T1>::value;
        static const size_t checkT2 = is_convertible_check<T2>::value;
        static_assert(checkT1 > 0, "is_convertible on incomplete type");
        static_assert(checkT2 > 0, "is_convertible on incomplete type");
    };
}

template <typename T1, typename T2>
struct is_convertible : detail::is_convertible<T1, T2> { };

/// move
/// Obtains an rvalue reference to its arguments and converts it to an xvalue.
/// Code which recieves an xvalue has the oppertunity to optimize away unnecessary
/// overhead by moving data out of the argument, leaving it in a valid, but
/// unspecified state.
template <typename T>
inline constexpr typename remove_reference<T>::type &&move(T &&t) {
    return static_cast<typename remove_reference<T>::type&&>(t);
}

}

#endif
