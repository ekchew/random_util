#ifndef RANDOM_UTIL_HPP
#define RANDOM_UTIL_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <random>
#include <type_traits>

/*---- SeedSource -------------------------------------------------------------
 *
 *  SeedSource is a namespace defining several possible sources of (hopefully)
 *  non-repeatable data with which to seed a pseudo-random number generator.
 *
 *  One or more of these sources can be used by the Seq struct (also defined
 *  within this namespace) to seed any of the generators defined in the
 *  <random> header. Seq complies with SeedSequence requirements
 *  (https://en.cppreference.com/w/cpp/named_req/SeedSequence).
 *
 *  Example:
 *
 *      SeedSource::Seq seq;
 *      std::minstd_rand lcg{seq};
 *      std::uniform_real_distribution dis;
 *      std::cout << dis(lcg) << '\n';
 *
 *  This should produce a different number in the range [0.0,1.0) every time it
 *  is run (assuming it doesn't fluke onto the same value due to the limits of
 *  printed precision).
 *
 *  Seq combines all sources by default. You can limit it to use say only
 *  std::random_device as a source with the appropriate flag:
 *
 *      SeedSource::Seq seq{SeedSource::kRandomDevice};
 */

namespace SeedSource {

    /*---- Seed source flags --------------------------------------------------
     *
     *  There are 3 seed sources defined by the following flags which can be
     *  combined using bitwise-OR.
     *
     *      kRandomDevice:
     *          This draws on std::random_device as a source.
     *
     *          Pros:
     *              - Ideally, this will generate quality random output from a
     *                source of system entropy like /dev/urandom.
     *              - This output can be used directly as the seed.
     *
     *          Cons:
     *              - The C++ standard is under-specified in terms of requiring
     *                quality output from std::random_device. In the worst-case
     *                scenario, a standard library implementation could even
     *                output the same sequence on every program run.
     *
     *      kSystemClock:
     *          This draws on std::chrono::system_clock::now() as a source.
     *          The value is converted to nanoseconds and eventually fed
     *          through std::seed_seq (possibly together with a steady clock
     *          time stamp) to generate a seed.
     *
     *          Pros:
     *              - This should provide a non-repeating value with which to
     *                seed the generator in nearly all cases.
     *
     *          Cons:
     *              - There is some small chance that a value could be repeated
     *                if the system clock adjusts itself due to time
     *                synchronization, leap seconds, etc.
     *              - A more sinister scenario might involve the clock being
     *                gamed to produce the same value every time.
     *
     *      kSteadyClock:
     *          This draws on std::chrono::steady_clock::now() instead of the
     *          system clock.
     *
     *          Pros:
     *              - Being steady, it should never repeat itself at least
     *                within a program run.
     *              - It may have a higher resolution than the system clock.
     *
     *          Cons:
     *              - In some implementations, the clock may be set to 0 when
     *                the program launches (as opposed to it counting time
     *                since boot-up for example). If the seed is taken at
     *                program launch, it may fall onto the same value or a
     *                limited range of values.
     *
     *      kAll:
     *          This combines all of the above flags. Seq defaults to this.
     */

    using Flags = std::uint_least32_t;

    inline constexpr Flags kRandomDevice = 0x00000001;
    inline constexpr Flags kSystemClock  = 0x00000002;
    inline constexpr Flags kSteadyClock  = 0x00000004;

    inline constexpr Flags kAll =
        kRandomDevice | kSystemClock | kSteadyClock;

    /*---- Seq struct ---------------------------------------------------------
     *
     *  This is a SeedSequence-compliant data structure whose only state
     *  variable contains some combination of the above flags.
     *
     *  Its primary constructor accepts a flags word which defaults to kAll.
     *  The other two are mostly there to comply with SeedSequence but let you
     *  specify flags as individual arguments if you like. For example, you
     *  could use the initializer list constructor and write:
     *
     *      Seq seq = {kSystemClock, kSteadyClock};
     *
     *  as opposed to:
     *
     *      Seq seq(kSystemClock | kSteadyClock);
     *
     *  Likewise, size() and param() also exist for compliance. size() returns
     *  1 (there is only 1 flags word) and param() simply writes the flags word
     *  to the output iterator.
     */

    struct Seq {
        using result_type = typename std::seed_seq::result_type;

        Flags flags;

        //---- Constructors ---------------------------------------------------

        constexpr Seq(Flags flags = kAll) noexcept: flags{flags} {}
        template<typename It, typename EndIt> constexpr
            Seq(It it, EndIt endIt) {
                for(this->flags = 0x00000000; it != endIt; ++it) {
                    this->flags |= *it;
                }
            }
        template<typename T> constexpr
            Seq(std::initializer_list<T> il):
                Seq{il.begin(), il.end()} {}

        /*---- generate method ------------------------------------------------
         *
         *  In a SeedSequence, the generate() method is responsible for filling
         *  a writeable random-access iterator range with seed data.
         *
         *  If the only source is kRandomDevice, the range is filled with
         *  32-bit integers generated by std::random_device.
         *
         *  If one or both of the clock sources are specified, the clock times
         *  are first converted to 64-bit nanosecond counts from the beginning
         *  of the epoch. These are then split into pairs of 32-bit integers,
         *  which are fed into a std::seed_seq. The latter is then used to
         *  generate output across the iterator range.
         *
         *  If both clock sources and the random device are specified, the
         *  clock-seeding algorithm is run first, followed by the random device
         *  algorithm modified to combine its output with the clocks' using
         *  bitwise-XOR.
         *
         *  (If no sources are specified, a default-constructed std::seed_seq
         *  generates the seed. This is not recommended since it will likely
         *  generate the same seed every time.)
         */

        template<typename RandomIt>
            void generate(RandomIt bgnIt, RandomIt endIt) const {
                using namespace std::chrono;

                //  This function feeds std::random_device output across the
                //  iterator range, but takes a call-back which handles whether
                //  to write the output directly or bitwise-XOR it.
                auto randDevGen = [&bgnIt, &endIt](auto fn) {
                    std::random_device rd;

                    //  Random device's native output is of type unsigned int.
                    //  This is most likely 32-bit already, but just to be
                    //  safe, it is passed through a 32-bit int distribution.
                    std::uniform_int_distribution<result_type>
                        dis{0x00000000, 0xffffffff};

                    for(auto it = bgnIt; it != endIt; ++it) {
                        fn(it, dis(rd));
                    }
                };

                //  This function takes a time point as returned by a
                //  std::chrono clock's now() method and outputs it as 2
                //  32-bit integers to an array iterator.
                auto extractTime = [](auto timePt, auto& arrIt) {
                    auto cnt = timePt.time_since_epoch().count();
                    *arrIt++ = static_cast<result_type>(cnt >> 32);
                    *arrIt++ = static_cast<result_type>(cnt);
                };

                //  Write std::random_device output directly over the iterator
                //  range if kRandomDevice is the only source selected.
                Flags f = this->flags & kAll;
                if(f == kRandomDevice) {
                    randDevGen([](auto it, result_type v) { *it = v; });
                }

                else {

                    //  Fill a temporary array with any selected clock times.
                    std::array<result_type,4> arr;
                    auto arrIt = arr.begin();
                    if(f & kSystemClock) {
                        extractTime(system_clock::now(), arrIt);
                    }
                    if(f & kSteadyClock) {
                        extractTime(steady_clock::now(), arrIt);
                    }

                    //  Fill the iterator range using a std::seed_seq.
                    //  This may be a default-constructed seed_seq if no clocks
                    //  were selected.
                    if(arrIt == arr.begin()) {
                        std::seed_seq().generate(bgnIt, endIt);
                    }
                    else {
                        std::seed_seq sseq(arr.begin(), arrIt);
                        sseq.generate(bgnIt, endIt);
                    }

                    //  Run a second pass over the iterator range if warranted
                    //  to XOR std::random_device output.
                    if(f & kRandomDevice) {
                        randDevGen([](auto it, result_type v) { *it ^= v; });
                    }
                }
            }

        //---------------------------------------------------------------------

        constexpr auto size() const noexcept -> std::size_t { return 1; }
        template<typename It> constexpr
            void param(It it) const { *it = this->flags; }
    };
};

/*---- MTEngine ---------------------------------------------------------------
 *
 *  MTEngine is a type_traits-like struct template which helps you choose which
 *  Mersenne Twister engine to use.
 *
 *  struct MTEngine type definitions:
 *      type: either std::mt19937 or std::mt19937_64
 *          This depends on whether you are compiling to a 64-bit target which,
 *          in turn, is determined by examining how many bits can fit into a
 *          std::size_t.
 *
 *  Global type definitions:
 *      MTEngineT: equivalent to MTEngine<>::type
 *
 *  Function definitions:
 *      MakeMTEngine:
 *          This factory function returns a Mersenne Twister engine seeded by
 *          a SeedSource::Seq.
 *
 *          Args:
 *              flags (SeedSource::Flags, optional): flags for Seq constructor
 *                  Defaults to SeedSource::kAll.
 *
 *          Returns:
 *              MTEngineT: the seeded Mersenne Twister
 *
 *          Example:
 *              auto mt = MakeMtEngine();
 *              std::uniform_real_distribution dis;
 *              std::cout << dis(mt) << '\n';
 *
 *              This is essentially equivalent to the example at the top of
 *              this header except it substitutes a Mersenne Twister for a
 *              linear congruential generator.
 */

template<typename Enable = void>
    struct MTEngine {
        using type = std::mt19937;
    };
template<>
    struct MTEngine<
        std::enable_if_t<std::numeric_limits<std::size_t>::digits >= 64U>
        >
    {
        using type = std::mt19937_64;
    };

using MTEngineT = typename MTEngine<>::type;

inline auto MakeMTEngine(SeedSource::Flags flags = SeedSource::kAll)
        -> MTEngineT
    {
        SeedSource::Seq seq(flags);
        return MTEngineT(seq);
    }

#endif
