/**
 * cppcmb.hpp
 *
 * @author Peter Lenkefi
 * @date 2018-09-05
 * @description Generic parser combinators for C++17.
 */

#ifndef CPPCMB_HPP
#define CPPCMB_HPP

#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

// XXX(LPeter1997): Make decltype(auto) where appropriate
// XXX(LPeter1997): We could make aliases for compile-time parser so we wouldn't
// need foo<T>::bar

// template <typename T>
// using foo_c = typename foo<T>::template bar<T>;

namespace cppcmb {

/**
 * Utilities and types used by the combinators.
 */
namespace detail {
	/**
	 * A custom optional wrapper to differentiate std::optional from nullables
	 * returned by a mapping failure.
	 */
	template <typename T>
	struct maybe : private std::optional<T> {
		constexpr maybe(maybe const&) = default;
		constexpr maybe(maybe&&) = default;

		maybe& operator=(maybe const&) = default;
		maybe& operator=(maybe&&) = default;

		maybe(std::optional<T> const&) = delete;
		maybe(std::optional<T>&&) = delete;

		std::optional<T>& operator=(std::optional<T> const&) = delete;
		std::optional<T>& operator=(std::optional<T>&&) = delete;

		using std::optional<T>::optional;
		using std::optional<T>::operator=;
		using std::optional<T>::operator->;
		using std::optional<T>::operator*;
		using std::optional<T>::operator bool;
		using std::optional<T>::has_value;
		using std::optional<T>::value;
		using std::optional<T>::value_or;
	};

	/**
	 * Helper functions to make the failable type. Wrapped in a struct so it can
	 * be inherited without duplication.
	 */
	struct maybe_ctors {
		template <typename T>
		static constexpr auto make_maybe(T&& val) {
			return maybe<std::decay_t<T>>(std::forward<T>(val));
		}

		template <typename T, typename... Args>
		static constexpr auto make_maybe(Args&&... args) {
			return maybe<T>(std::in_place, std::forward<Args>(args)...);
		}

		template <typename T, typename U, typename... Args>
		static constexpr auto
		make_maybe(std::initializer_list<U> il, Args&&... args) {
			return maybe<T>(std::in_place, il, std::forward<Args>(args)...);
		}
	};

	/**
	 * A helper functionality that wraps any value into a tuple if it's not
	 * already a tuple.
	 */
	template <typename T>
	struct as_tuple_impl {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return std::make_tuple(std::forward<TFwd>(arg));
		}
	};

	template <typename... Ts>
	struct as_tuple_impl<std::tuple<Ts...>> {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return arg;
		}
	};

	template <typename TFwd>
	constexpr auto as_tuple(TFwd&& arg) {
		return as_tuple_impl<std::decay_t<TFwd>>::pass(std::forward<TFwd>(arg));
	}

	/**
	 * A helper functionality that unwraps a tuple if it can.
	 */
	template <typename TFwd>
	constexpr auto unwrap_tuple(TFwd&& arg);

	template <typename T>
	struct unwrap_tuple_impl {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return arg;
		}
	};

	template <typename T>
	struct unwrap_tuple_impl<std::tuple<T>> {
		template <typename TFwd>
		static constexpr auto pass(TFwd&& arg) {
			return unwrap_tuple(std::get<0>(std::forward<TFwd>(arg)));
		}
	};

	template <typename TFwd>
	constexpr auto unwrap_tuple(TFwd&& arg) {
		return unwrap_tuple_impl<std::decay_t<TFwd>>::pass(
			std::forward<TFwd>(arg)
		);
	}

	/**
	 * Identity function.
	 */
	template <typename T>
	constexpr auto identity(T&& arg) {
		return arg;
	}

	/**
	 * A function that constructs a tuple if there are multiple or 0 parameters,
	 * but returns the parameter itself if there is only one.
	 */
	template <typename... Ts>
	constexpr auto tuple_of(Ts&&... args) {
		if constexpr (sizeof...(args) == 1) {
			return identity(std::forward<Ts>(args)...);
		}
		else {
			return std::make_tuple(std::forward<Ts>(args)...);
		}
	}

	/**
	 * Concatenates the arguments into a tuple and unwraps the result if
	 * possible.
	 */
	template <typename... Ts>
	constexpr auto concat(Ts&&... args) {
		return unwrap_tuple(
			std::tuple_cat(as_tuple(std::forward<Ts>(args))...)
		);
	}

	/**
	 * Wraps a free function into a functor type so we can pass it around as a
	 * type. Every non-combinator function (like transformations) has to be
	 * wrapped in this.
	 */
	template <auto Callable>
	struct fn_wrap {
		constexpr fn_wrap() = default;

		template <typename... Ts>
		constexpr auto operator()(Ts&&... args) const {
			return Callable(std::forward<Ts>(args)...);
		}
	};

	/**
	 * We need to forward-declare functionality for the subscript operator.
	 */
	template <typename Combinator, typename Mapper>
	constexpr auto make_subscript_map(Combinator&&, Mapper&&);

	/**
	 * Wraps a free function to act as a combinator function. Every combinator
	 * must be wrapped in this.
	 */
	template <typename TokenIterator, auto Callable>
	struct cmb_wrap {
	private:
		using callable_type = std::decay_t<decltype(Callable)> ;
		static_assert(
			std::is_invocable_v<callable_type, TokenIterator>,
			"A combinator must be able to be invoked with the provided iterator"
			" type!"
		);
		using return_type = std::invoke_result_t<callable_type, TokenIterator>;
		using pair_type = typename return_type::value_type;

	public:
		using data_type = typename pair_type::first_type;
		using iterator_type = typename pair_type::second_type;

		static_assert(
			std::is_same_v<TokenIterator, iterator_type>,
			"The resulting iterator type must match the provided one!"
		);

		constexpr cmb_wrap() = default;

		constexpr auto operator()(TokenIterator it) const {
			return Callable(it);
		}

		template <typename Mapper>
		constexpr auto operator[](Mapper&& m) const {
			return make_subscript_map(*this, std::forward<Mapper>(m));
		}
	};

	/**
	 * Check if a type is a function pointer.
	 */
	template <typename T>
	struct is_function_ptr : std::bool_constant<
		std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>
	> { };

	template <typename T>
	inline constexpr bool is_function_ptr_v = is_function_ptr<T>::value;

	/**
	 * Filters the result. If the predicate is true, it succeeds, fails
	 * otherwise.
	 */
	template <typename Predicate>
	struct filter_impl {
		static_assert(
			!is_function_ptr_v<Predicate>,
			"Filter cannot have a function pointer as a predicate!"
		);

		constexpr filter_impl() = default;

		template <typename... Ts>
		constexpr auto operator()(Ts&&... args) const {
			using res_type = decltype(maybe_ctors::make_maybe(
				tuple_of(std::forward<Ts>(args)...)
			));
			if (Predicate()(args...)) {
				return maybe_ctors::make_maybe(
					tuple_of(std::forward<Ts>(args)...)
				);
			}
			return res_type();
		}
	};

	/**
	 * Selector function that returns some elements of the tuple.
	 */
	template <std::size_t... Indicies>
	struct select_impl {
		constexpr select_impl() = default;

		template <typename... Ts>
		constexpr auto operator()(Ts&&... args) const {
			return tuple_of(std::get<Indicies>(
				std::make_tuple(std::forward<Ts>(args)...)
			)...);
		}
	};

	/**
	 * Fold left function.
	 * f(f(f(f(f(..., f), g), h), i), j)
	 */
	template <typename Folder>
	struct foldl_impl {
		static_assert(
			!is_function_ptr_v<Folder>,
			"Fold function cannot have a function pointer as a predicate!"
		);

		constexpr foldl_impl() = default;

		template <typename Init, typename Rest>
		constexpr auto operator()(Init&& first, Rest&& rest) const {
			for (auto it = std::cbegin(rest); it != std::cend(rest); ++it) {
				first = Folder()(std::move(first), *it);
			}
			return first;
		}
	};

	/**
	 * Fold right function.
	 * f(a, f(b, f(c, f(d, f(e, ...)))))
	 */
	template <typename Folder>
	struct foldr_impl {
		static_assert(
			!is_function_ptr_v<Folder>,
			"Fold function cannot have a function pointer as a predicate!"
		);

		constexpr foldr_impl() = default;

		template <typename Init, typename Rest>
		constexpr auto operator()(Init&& rest, Rest&& first) const {
			for (auto it = std::crbegin(rest); it != std::crend(rest); ++it) {
				first = Folder()(*it, std::move(first));
			}
			return first;
		}
	};

	/**
	 * Mechanism for matching maybes. Mapping combinator needs it.
	 */
	template <typename T>
	struct is_maybe : std::false_type {};

	template <typename T>
	struct is_maybe<maybe<T>> : std::true_type {};

	template <typename T>
	inline constexpr bool is_maybe_v = is_maybe<T>::value;

	/**
	 * Mechanism for matching combinators. This is the reason we need all
	 * combinators as cmb_wrap-s.
	 */
	template <typename T>
	struct is_cmb : std::false_type {};

	template <typename TokenIterator, auto Callable>
	struct is_cmb<cmb_wrap<TokenIterator, Callable>> : std::true_type {};

	template <typename T>
	inline constexpr bool is_cmb_v = is_cmb<T>::value;

	/**
	 * SFINAE test for combinators.
	 */
	template <typename T>
	struct enable_if_cmb : public std::enable_if<
		is_cmb_v<std::decay_t<T>>
	> {};

	template <typename T>
	using enable_if_cmb_t = typename enable_if_cmb<T>::type;

// Defaults so we can have operators
#ifndef cppcmb_default_rep_container
	template <typename T>
	using rep_container_t = std::vector<T>;
#else
	template <typename T>
	using rep_container_t = cppcmb_default_rep_container<T>;
#endif

#ifndef cppcmb_default_rep1_container
	template <typename T>
	using rep1_container_t = std::vector<T>;
#else
	template <typename T>
	using rep1_container_t = cppcmb_default_rep1_container<T>;
#endif

} /* namespace detail */

/**
 * A module interface for compile-time parser combinators.
 */
template <template <typename...> typename Collection>
struct combinator_comptime {
public:
	/**
	 * Result type is a triplet of the success flag, the parsing result and the
	 * remaining input.
	 */
	template <bool Succ, typename Res, typename Rem>
	struct result_type {
		static constexpr bool success = Succ;
		using result = Res;
		using remaining = Rem;
	};

	/**
	 * Shortcut for succeeding result.
	 */
	template <typename Res, typename Rem>
	using success_result = result_type<true, Res, Rem>;

	/**
	 * Shortcut for failing result. We default the result to void and the
	 * remaining input to empty container.
	 */
	using fail_result = result_type<false, void, Collection<>>;

private:
	/**
	 * Check if a type is a collection.
	 */
	template <typename>
	struct is_collection : std::false_type {};

	template <typename... Ts>
	struct is_collection<Collection<Ts...>> : std::true_type {};

	template <typename T>
	static constexpr bool is_collection_v = is_collection<T>::value;

	/**
	 * Utility to concatenate resulting collections.
	 * We need this because of ambiguous instantiations...
	 */
	template <bool, bool, typename U, typename V>
	struct concat_result_impl {
		using type = Collection<U, V>;
	};

	template <typename U, typename... Vs>
	struct concat_result_impl<false, true, U, Collection<Vs...>> {
		using type = Collection<U, Vs...>;
	};

	template <typename... Us, typename V>
	struct concat_result_impl<true, false, Collection<Us...>, V> {
		using type = Collection<Us..., V>;
	};

	template <typename... Us, typename... Vs>
	struct concat_result_impl<true, true,
		Collection<Us...>, Collection<Vs...>> {
		using type = Collection<Us..., Vs...>;
	};

	template <typename U, typename V>
	struct concat_result
		: concat_result_impl<is_collection_v<U>, is_collection_v<V>, U, V> {};

	/**
	 * Unwrap a single collection element into that single element.
	 */
	template <typename T>
	struct unwrap_single {
		using type = T;
	};

	template <typename T>
	struct unwrap_single<Collection<T>> {
		using type = typename unwrap_single<T>::type;
	};

	template <typename T>
	using unwrap_single_t = typename unwrap_single<T>::type;

public:
	template <typename U, typename V>
	using concat_result_t = unwrap_single_t<typename concat_result<U, V>::type>;

private:
	/**
	 * We need a one-implementation here to have a dummy type to be able to
	 * specialize it explicitly.
	 */
	template <typename, typename>
	struct one_impl;

	template <typename Dummy>
	struct one_impl<Collection<>, Dummy> : fail_result {};

	template <typename First, typename... Rest, typename Dummy>
	struct one_impl<Collection<First, Rest...>, Dummy>
		: success_result<First, Collection<Rest...>> {};

	/**
	 * Sequencing two combinators together.
	 */
	template <template <typename> typename P1, template <typename> typename P2>
	struct mbind2 {
		template <typename, bool, typename, typename>
		struct call2;

		template <typename OldRes, typename NewRes, typename Rem>
		struct call2<OldRes, false, NewRes, Rem> : fail_result {};

		template <typename OldRes, typename NewRes, typename Rem>
		struct call2<OldRes, true, NewRes, Rem>
			: success_result<concat_result_t<OldRes, NewRes>, Rem> {};

		template <bool, typename, typename>
		struct call1;

		template <typename Res, typename Rem>
		struct call1<false, Res, Rem> : fail_result {};

		template <typename Res, typename Rem>
		struct call1<true, Res, Rem>
			: call2<Res, P2<Rem>::success,
			typename P2<Rem>::result, typename P2<Rem>::remaining> {};

		template <typename>
		struct type;

		template <typename... Ts>
		struct type<Collection<Ts...>>
			: call1<
				P1<Collection<Ts...>>::success,
				typename P1<Collection<Ts...>>::result,
				typename P1<Collection<Ts...>>::remaining
			> {};
	};

public:
	/**
	 * The simplest combinator that succeeds with an empty result.
	 */
	template <typename>
	struct succ;

	template <typename... Ts>
	struct succ<Collection<Ts...>>
		: success_result<Collection<>, Collection<Ts...>> {};

	/**
	 * A combinator that always fails.
	 */
	template <typename>
	struct fail;

	template <typename... Ts>
	struct fail<Collection<Ts...>> : fail_result {};

	/**
	 * A combinator that returns the current token and advances the position by
	 * one. Fails if there is no more input.
	 */
	template <typename In>
	struct one : one_impl<In, void> {};

	/**
	 * Wraps another combinator so that it becomes optional. This combinator
	 * therefore always succeeds but does not always yield a result.
	 */
	template <template <typename> typename P>
	struct opt {
	private:
		template <bool, typename, typename, typename>
		struct call;

		template <typename Res, typename OldRem, typename NewRem>
		struct call<false, Res, OldRem, NewRem>
			: success_result<Collection<>, OldRem> {};

		template <typename Res, typename OldRem, typename NewRem>
		struct call<true, Res, OldRem, NewRem>
			: success_result<Res, NewRem> {};

	public:
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>>
			: call<
				P<Collection<Ts...>>::success,
				typename P<Collection<Ts...>>::result,
				Collection<Ts...>,
				typename P<Collection<Ts...>>::remaining
			> {};
	};

	/**
	 * Applies combinators in a sequence and concatenates the results if all of
	 * them succeeds. If one fails, the whole sequence fails.
	 */
	template <template <typename> typename...>
	struct seq;

	template <template <typename> typename P1, template <typename> typename P2,
		template <typename> typename... PRest>
	struct seq<P1, P2, PRest...>
		: seq<mbind2<P1, P2>::template type, PRest...> {};

	template <template <typename> typename P1>
	struct seq<P1> {
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>> : P1<Collection<Ts...>> {};
	};

	/**
	 * Applies the combinators and returns with the first succeeding one. If
	 * none of them succeeds, the combinator fails.
	 */
	template <template <typename> typename...>
	struct alt;

	template <template <typename> typename PFirst,
		template <typename> typename... PRest>
	struct alt<PFirst, PRest...> {
	private:
		template <bool, typename, typename, typename>
		struct call;

		template <typename Res, typename OldRem, typename NewRem>
		struct call<false, Res, OldRem, NewRem>
			: alt<PRest...>::template cmb<OldRem> {};

		template <typename Res, typename OldRem, typename NewRem>
		struct call<true, Res, OldRem, NewRem>
			: success_result<Res, NewRem> {};

	public:
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>>
			: call<
				PFirst<Collection<Ts...>>::success,
				typename PFirst<Collection<Ts...>>::result,
				Collection<Ts...>,
				typename PFirst<Collection<Ts...>>::remaining
			> {};
	};

	template <template <typename> typename P>
	struct alt<P> {
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>> : P<Collection<Ts...>> {};
	};

	/**
	 * Repeatedly applies a combinator while it succeeds, concatenates results.
	 * Stops on faliure.
	 */
	template <template <typename> typename P>
	struct rep {
	private:
		template <bool, typename, typename, typename, typename>
		struct call;

		template <typename OldRes, typename OldRem,
			typename NewRes, typename NewRem>
		struct call<false, OldRes, OldRem, NewRes, NewRem>
			: success_result<unwrap_single_t<OldRes>, OldRem> {};

		template <typename OldRes, typename OldRem,
			typename NewRes, typename NewRem>
		struct call<true, OldRes, OldRem, NewRes, NewRem>
			: call<P<NewRem>::success,
				NewRes, NewRem,
				concat_result_t<NewRes, typename P<NewRem>::result>,
				typename P<NewRem>::remaining> {};

	public:
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>>
			: call<true,
				Collection<>, Collection<Ts...>,
				Collection<>, Collection<Ts...>
			> {};
	};

	/**
	 * Repeatedly applies a combinator while it succeeds. Stops on faliure.
	 * Collects result into a collection. Succeeds if it collected at least one
	 * element.
	 */
	template <template <typename> typename P>
	struct rep1 {
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>>
			: seq<P, rep<P>::template cmb>::
			template cmb<Collection<Ts...>> {};
	};

	/**
	 * Types to mark a mapping that fails.
	 */
	struct map_fail {};

	/**
	 * Applies a combinator. If it succeeded, the result data is applied to a
	 * transformation function. If the transformation can fail, then it's also
	 * considered when returning (on transformation failure the combinator
	 * fails).
	 */
	template <template <typename> typename P, template <typename...> typename M>
	struct map {
	private:
		// General non-failing case
		template <typename Res, typename Rem>
		struct applied_result : success_result<unwrap_single_t<Res>, Rem> {};

		// Fail case
		template <typename Rem>
		struct applied_result<map_fail, Rem> : fail_result {};

		//////////

		template <typename Res>
		struct apply_map {
			using type = typename M<Res>::type;
		};

		template <typename... Ts>
		struct apply_map<Collection<Ts...>> {
			using type = typename M<Ts...>::type;
		};

		template <typename Res>
		using apply_map_t = typename apply_map<Res>::type;

		//////////

		template <bool, typename, typename>
		struct call;

		template <typename Res, typename Rem>
		struct call<false, Res, Rem> : fail_result {};

		template <typename Res, typename Rem>
		struct call<true, Res, Rem> : applied_result<apply_map_t<Res>, Rem> {};

	public:
		template <typename>
		struct cmb;

		template <typename... Ts>
		struct cmb<Collection<Ts...>>
			: call<
				P<Collection<Ts...>>::success,
				typename P<Collection<Ts...>>::result,
				typename P<Collection<Ts...>>::remaining
			> {};
	};

	/**
	 * Filters the result. If the predicate is true, it succeeds, fails
	 * otherwise.
	 */
	template <template <typename...> typename Pred>
	struct filter {
	private:
		template <bool, typename...>
		struct call;

		template <typename... Ts>
		struct call<true, Ts...> {
			using type = unwrap_single_t<Collection<Ts...>>;
		};

		template <typename... Ts>
		struct call<false, Ts...> {
			using type = map_fail;
		};

	public:
		template <typename... Ts>
		struct fn : call<Pred<Ts...>::value, Ts...> {};
	};

	/**
	 * Selector function that returns some elements of the result.
	 */
	template <std::size_t... Indicies>
	struct select {
		template <typename... Ts>
		struct fn {
			using type = unwrap_single_t<Collection<
				std::tuple_element_t<Indicies, std::tuple<Ts...>>...
			>>;
		};
	};
};

/**
 * A module interface for a template-style grammar definition.
 */
template <typename TokenIterator>
struct combinator_types : private detail::maybe_ctors {
private:
	using iterator_category = typename
		std::iterator_traits<TokenIterator>::iterator_category;
	static_assert(
		std::is_base_of_v<std::forward_iterator_tag, iterator_category>,
		"The module requires a forward iterator!"
	);

public:
	/**
	 * General result type.
	 */
	template <typename... Data>
	using result_type = std::optional<std::pair<
		decltype(detail::tuple_of(std::declval<Data>()...)),
		TokenIterator
	>>;

	/**
	 * Creates a result value.
	 * Result values are in the form of ((data...), position)?.
	 */
	template <typename Data>
	static constexpr auto make_result(Data&& data, TokenIterator it) {
		return std::make_optional(std::make_pair(
			std::forward<Data>(data), it
		));
	}

private:
	/**
	 * The simplest combinator that succeeds with an empty result.
	 */
	struct cmb_succ_fn_aux {
		static inline constexpr auto cmb_succ_fn(TokenIterator it) {
			return make_result(std::make_tuple(), it);
		}
	};

	/**
	 * A combinator that always fails with a given data type.
	 */
	template <typename T>
	struct cmb_fail_helper {
		static inline constexpr auto cmb_fail_fn1(TokenIterator) {
			return result_type<T>();
		}
	};

	/**
	 * A combinator that returns the current token and advances the position by
	 * one.
	 */
	struct cmb_one_fn_aux {
		static constexpr auto cmb_one_fn(TokenIterator it) {
			return make_result(*it, std::next(it));
		}
	};

	/**
	 * Wraps another combinator so that it's return data becomes optional. This
	 * combinator therefore always succeeds.
	 */
	template <typename Combinator>
	struct cmb_opt_fn_aux {
		static constexpr auto cmb_opt_fn(TokenIterator it) {
			static_assert(
				detail::is_cmb_v<Combinator>,
				"Optional combinator requires a combinator wrapper as argument!"
				);

			if (auto res = Combinator()(it)) {
				return make_result(std::make_optional(res->first), res->second);
			}
			using data_type = typename Combinator::data_type;
			return make_result(std::optional<data_type>(), it);
		}
	};
	/**
	 * Applies combinators in a sequence and concatenates the results if all of
	 * them succeeds. If one fails, the whole sequence fails.
	 */
	template <typename First, typename... Rest>
	struct cmb_seq_fn_aux {
		static constexpr auto cmb_seq_fn(TokenIterator it) {
			static_assert(
				detail::is_cmb_v<First>,
				"Sequencing combinator requires a combinator wrapper as argument!"
				);

			if constexpr (sizeof...(Rest) == 0) {
				// Only one entry, return that
				return First()(it);
			}
			else {
				using first_type = typename First::data_type;
				using data_type = decltype(detail::concat(
					std::declval<first_type>(), cmb_seq_fn_aux<Rest...>::cmb_seq_fn(it)->first
				));
				if (auto first = First()(it)) {
					if (auto rest = cmb_seq_fn_aux<Rest...>::cmb_seq_fn(first->second)) {
						return make_result(
							detail::concat(
								std::move(first->first), std::move(rest->first)
							),
							rest->second
						);
					}
				}
				return fail<data_type>()(it);
			}
		}
	};

	/**
	 * Applies the combinators and returns with the first succeeding one. If
	 * none of them succeeds, the combinator fails.
	 */
	template <typename ResultData, typename First, typename... Rest>
	struct cmb_alt_fn_aux {
		static constexpr auto cmb_alt_fn(TokenIterator it) {
			static_assert(
				detail::is_cmb_v<First>,
				"Alternative combinator requires a combinator wrapper as argument!"
				);

			if constexpr (sizeof...(Rest) == 0) {
				// Just this one entry is left
				return First()(it);
			}
			else {
				if (auto first = First()(it)) {
					return first;
				}
				return cmb_alt_fn_aux<ResultData, Rest...>::cmb_alt_fn(it);
			}
		}
	};

	/**
	 * Repeatedly applies a combinator while it succeeds. Stops on faliure.
	 * Collects result into a collection. Always succeeds.
	 */
	template <template <typename...> typename Collection, typename Combinator>
	struct cmb_rep_fn_aux {
		static constexpr auto cmb_rep_fn(TokenIterator it) {
			static_assert(
				detail::is_cmb_v<Combinator>,
				"Repeat combinator requires a combinator wrapper as argument!"
				);

			using element_type = typename Combinator::data_type;
			Collection<element_type> result;
			auto out_it = std::back_inserter(result);
			while (true) {
				auto res = Combinator()(it);
				if (!res) {
					return make_result(std::move(result), it);
				}
				// Advance
				*out_it++ = std::move(res->first);
				it = res->second;
			}
		}
	};

	/**
	 * Repeatedly applies a combinator while it succeeds. Stops on faliure.
	 * Collects result into a collection. Succeeds if it collected at least one
	 * element.
	 */
	template <template <typename...> typename Collection, typename Combinator>
	struct cmb_rep1_fn_aux {
		static constexpr auto cmb_rep1_fn(TokenIterator it) {
			auto res = cmb_rep_fn_aux<Collection, Combinator>::cmb_rep_fn(it);
			using res_type = decltype(res);
			if (res->first.empty()) {
				// Empty, fail
				return res_type();
			}
			return res;
		}
	};

	/**
	 * Applies a combinator. If it succeeded, the result data is applied to a
	 * transformation function. If the transformation can fail, then it's also
	 * considered when returning (on transformation failure the combinator
	 * fails).
	 */
	template <typename Combinator, typename Mapper>
	struct cmb_map_fn_aux {
		static constexpr auto cmb_map_fn(TokenIterator it) {
			static_assert(
				detail::is_cmb_v<Combinator>,
				"Map combinator requires a combinator wrapper as argument!"
				);
			static_assert(
				!detail::is_function_ptr_v<std::decay_t<Mapper>>,
				"Map does not accpet raw function pointers as transformations!"
				);

			using data_type = typename Combinator::data_type;
			using transform_result = decltype(std::apply(
				Mapper(), detail::as_tuple(std::declval<data_type>())
			));

			if constexpr (detail::is_maybe_v<transform_result>) {
				if (auto res = Combinator()(it)) {
					if (auto transformed =
						std::apply(Mapper(), detail::as_tuple(res->first))) {
						return make_result(
							detail::unwrap_tuple(std::move(*transformed)),
							res->second
						);
					}
				}
				// XXX(LPeter1997): Simplify?
				using result_type = decltype(detail::unwrap_tuple(*std::apply(
					Mapper(), detail::as_tuple(std::declval<data_type>())
				)));
				return fail<result_type>()(it);
			}
			else {
				if (auto res = Combinator()(it)) {
					return make_result(
						detail::unwrap_tuple(
							std::apply(Mapper(), detail::as_tuple(res->first))
						),
						res->second
					);
				}
				// XXX(LPeter1997): Simplify?
				using result_type = decltype(detail::unwrap_tuple(std::apply(
					Mapper(), detail::as_tuple(std::declval<data_type>())
				)));
				return fail<result_type>()(it);
			}
		}
	};

public:
	template <typename T>
	using maybe = detail::maybe<T>;

	using detail::maybe_ctors::make_maybe;

	template <auto Fn>
	using fn = detail::fn_wrap<Fn>;

	template <auto Fn>
	using cmb = detail::cmb_wrap<TokenIterator, Fn>;

	using succ = cmb<cmb_succ_fn_aux::cmb_succ_fn>;

	template <typename T>
	using fail = cmb<cmb_fail_helper<T>::cmb_fail_fn1>;

	using one = cmb<cmb_one_fn_aux::cmb_one_fn>;

	template <typename Combinator>
	using opt = cmb<cmb_opt_fn_aux<Combinator>::cmb_opt_fn>;

	template <typename First, typename... Rest>
	using seq = cmb<cmb_seq_fn_aux<First, Rest...>::cmb_seq_fn>;

	template <typename First, typename... Rest>
	using alt = cmb<cmb_alt_fn_aux<typename First::data_type, First, Rest...>::cmb_alt_fn>;

	template <template <typename...> typename Collection, typename Combinator>
	using rep = cmb<cmb_rep_fn_aux<Collection, Combinator>::cmb_rep_fn>;

	template <template <typename...> typename Collection, typename Combinator>
	using rep1 = cmb<cmb_rep1_fn_aux<Collection, Combinator>::cmb_rep1_fn>;

	template <typename Combinator, typename Mapper>
	using map = cmb<cmb_map_fn_aux<Combinator, Mapper>::cmb_map_fn>;

	template <typename Predicate>
	using filter = detail::filter_impl<Predicate>;

	template <std::size_t... Indicies>
	using select = detail::select_impl<Indicies...>;

	template <typename Folder>
	using foldl = detail::foldl_impl<Folder>;

	template <typename Folder>
	using foldr = detail::foldr_impl<Folder>;
};

template <typename TokenIterator>
struct combinator_values : private detail::maybe_ctors {
	using types = combinator_types<TokenIterator>;

	template <typename... Data>
	using result_type = typename types::template result_type<Data...>;

	template <typename T>
	using maybe = typename types::template maybe<T>;

	using detail::maybe_ctors::make_maybe;

	template <auto Fn>
	static constexpr auto cmb = typename types::template cmb<Fn>();

	template <auto Fn>
	static constexpr auto fn = typename types::template fn<Fn>();

	static constexpr auto succ = typename types::succ();

	static constexpr auto one = typename types::one();

	template <typename T>
	static constexpr auto fail = typename types::template fail<T>();

	template <typename Combinator>
	static constexpr auto opt(Combinator&&) {
		return typename types::template opt<std::decay_t<Combinator>>();
	}

	template <typename... Combinators>
	static constexpr auto seq(Combinators&&...) {
		return typename types::template seq<std::decay_t<Combinators>...>();
	}

	template <typename First, typename... Rest>
	static constexpr auto alt(First&&, Rest&&...) {
		return typename types::template alt<
			std::decay_t<First>, std::decay_t<Rest>...>();
	}

	template <template <typename...> typename Collection, typename Combinator>
	static constexpr auto rep(Combinator&&) {
		return typename types::template rep<Collection,
			std::decay_t<Combinator>>();
	}

	template <template <typename...> typename Collection, typename Combinator>
	static constexpr auto rep1(Combinator&&) {
		return typename types::template rep1<Collection,
			std::decay_t<Combinator>>();
	}

	template <typename Combinator, typename Mapper>
	static constexpr auto map(Combinator&&, Mapper&&) {
		return typename types::template map<std::decay_t<Combinator>,
			std::decay_t<Mapper>>();
	}

	template <typename Predicate>
	static constexpr auto filter(Predicate&&) {
		return typename types::template filter<std::decay_t<Predicate>>();
	}

	template <std::size_t... Indicies>
	static constexpr auto select =
		typename types::template select<Indicies...>();

	template <typename Folder>
	static constexpr auto foldl(Folder&&) {
		return typename types::template foldl<std::decay_t<Folder>>();
	}

	template <typename Folder>
	static constexpr auto foldr(Folder&&) {
		return typename types::template foldr<std::decay_t<Folder>>();
	}
};

/**
 * We can implement the subscript hack here.
 */
namespace detail {
	template <typename Combinator, typename Mapper>
	constexpr auto make_subscript_map(Combinator&& c, Mapper&& m) {
		using combinator_t = std::decay_t<Combinator>;
		using iter_type = typename combinator_t::iterator_type;

		return combinator_values<iter_type>::map(
			std::forward<Combinator>(c), std::forward<Mapper>(m));
	}
} /* namespace detail */

} /* namespace cppcmb */

/**
 * Operators for nicer syntax.
 */

/**
 * Optional.
 */
template <typename Combinator,
	typename = cppcmb::detail::enable_if_cmb_t<Combinator>>
constexpr auto operator~(Combinator&& c) {
	using combinator_t = std::decay_t<Combinator>;

	using iter_type = typename combinator_t::iterator_type;

	return cppcmb::combinator_values<iter_type>::opt(
		std::forward<Combinator>(c)
	);
}

/**
 * Sequencing.
 */
template <typename Left, typename Right,
	typename = cppcmb::detail::enable_if_cmb_t<Left>,
	typename = cppcmb::detail::enable_if_cmb_t<Right>>
constexpr auto operator&(Left&& l, Right&& r) {
	using left_t = std::decay_t<Left>;
	using right_t = std::decay_t<Right>;

	using iter_type = typename left_t::iterator_type;

	static_assert(
		std::is_same_v<iter_type, typename right_t::iterator_type>,
		"Sequenced iterator types must match!");

	return cppcmb::combinator_values<iter_type>::seq(
		std::forward<Left>(l), std::forward<Right>(r)
	);
}

/**
 * Alternatives.
 */
template <typename Left, typename Right,
	typename = cppcmb::detail::enable_if_cmb_t<Left>,
	typename = cppcmb::detail::enable_if_cmb_t<Right>>
constexpr auto operator|(Left&& l, Right&& r) {
	using left_t = std::decay_t<Left>;
	using right_t = std::decay_t<Right>;

	using iter_type = typename left_t::iterator_type;

	static_assert(
		std::is_same_v<iter_type, typename right_t::iterator_type>,
		"Alternate iterator types must match!");

	return cppcmb::combinator_values<iter_type>::alt(
		std::forward<Left>(l), std::forward<Right>(r)
	);
}

/**
 * Repetition (0 or more).
 */
template <typename Arg,
	typename = cppcmb::detail::enable_if_cmb_t<Arg>>
constexpr auto operator*(Arg&& a) {
	using arg_t = std::decay_t<Arg>;
	using iter_type = typename arg_t::iterator_type;

	return cppcmb::combinator_values<iter_type>::
	template rep<cppcmb::detail::rep_container_t>(
		std::forward<Arg>(a)
	);
}

/**
 * Repetition (1 or more).
 */
template <typename Arg,
	typename = cppcmb::detail::enable_if_cmb_t<Arg>>
constexpr auto operator+(Arg&& a) {
	using arg_t = std::decay_t<Arg>;
	using iter_type = typename arg_t::iterator_type;

	return cppcmb::combinator_values<iter_type>::
	template rep1<cppcmb::detail::rep1_container_t>(
		std::forward<Arg>(a)
	);
}

#endif /* CPPCMB_HPP */
