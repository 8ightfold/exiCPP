//===- Driver.cpp ---------------------------------------------------===//
//
// Copyright (C) 2024 Eightfold
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
//     limitations under the License.
//
//===----------------------------------------------------------------===//

#include <Common/Box.hpp>
#include <Common/Map.hpp>
#include <Common/String.hpp>
#include <Common/SmallStr.hpp>
#include <Common/SmallVec.hpp>
#include <Common/StringSwitch.hpp>
#include <Support/Alignment.hpp>
#include <Support/Allocator.hpp>
#include <Support/Chrono.hpp>
#include <Support/Casting.hpp>
#include <Support/Debug.hpp>
#include <Support/Error.hpp>
#include <Support/Filesystem.hpp>
#include <Support/FmtBuffer.hpp>
#include <Support/MemoryBufferRef.hpp>
#include <Support/Process.hpp>
#include <Support/raw_ostream.hpp>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/ranges.h>

#include <exi/Stream/BitStream.hpp>

#include <tuple>
#include <malloc.h>
#include <windows.h>

// TODO: Tests!!

using namespace exi;
using namespace exi::sys;

#if EXI_USE_MIMALLOC
static bool ITestMimallocRedirect(usize Mul) {
  bool Result = true;
  if (!mi_is_redirected())
    return true;
  {
    void* Alloc = malloc(16 * Mul);
    if (void* New = _expand(Alloc, 32 * Mul))
      free(New);
    else
      free(Alloc);
  } {
    void* Alloc = malloc(16 * Mul);
    void* New = realloc(Alloc, 512 * Mul);
    free(New);
  } {
    // void* Alloc = malloc(16 * Mul);
    // void* New = _recalloc(Alloc, 64, 4 * Mul);
    // free(New);
  } {
    constexpr Align A = 32_align;
    void* Alloc = _aligned_malloc(16 * Mul, A.value());
    if (!isAddrAligned(A, Alloc))
      Result = false;
    void* New = _aligned_realloc(Alloc, 64 * Mul, A.value());
    if (!isAddrAligned(A, Alloc))
      Result = false;
    _aligned_free(New);
  }
  return Result;
}

static bool testMimallocRedirect() {
  constexpr usize kMaxMul = 20'000'000;
  usize Mul = 1;

  bool Result = true;
  outs() << "Running tests...\n";
  while (Mul < kMaxMul) {
    if (ITestMimallocRedirect(Mul)) {
      outs() << "Test " << Mul << " passed.\n"; 
    } else {
      outs() << "Test " << Mul << " failed.\n"; 
      Result = false;
    }
    Mul *= 2;
  }

  if (Result)
    outs() << "All tests passed!\n"; 
  return Result;
}

static void printIfInHeap(const void* Ptr) {
  if (mi_is_in_heap_region(Ptr))
    fmt::println("\"{}\" in heap!", Ptr);
  else
    fmt::println("\"{}\" not in heap!", Ptr);
}
#else
# define printIfInHeap(...) (void(0))
#endif // EXI_USE_MIMALLOC

static void miscTests(int Argc, char* Argv[]) {
#if EXI_USE_MIMALLOC
  if (mi_option_is_enabled(mi_option_verbose)) {
    mi_option_disable(mi_option_verbose);
    outs() << '\n';
  }
  if (!mi_is_redirected()) {
    outs() << "Redirection failed.\n";
  } else {
    outs() << "Is redirected!\n";
    testMimallocRedirect();
  }
  outs() << '\n';
#endif // EXI_USE_MIMALLOC

  SmallStr<256> Str;
  fs::current_path(Str);
  outs() << Argv[0] << '\n';
  fmt::println("{}", Str);

  TimePoint<> TP = sys::now();
  fmt::println("TimePoint<>: {}", TP);
  TimePoint<> TP2 = sys::now();
  fmt::println("Duration: {}", (TP2 - TP));
  outs() << TP << ", " << TP2 << '\n';

  {
    auto* P = (char*)exi::allocate_buffer(4096, 16);
    printIfInHeap(P);
    exi::deallocate_buffer(P, 4096, 16);
  }

  std::string SStr;
  wrap_stream(SStr) << "Hello world!\nIt's me!";
  printIfInHeap(SStr.data());

  errs() << "\n\n";
  errs() << "mimalloc: " << Process::GetMallocUsage() << '\n';
  errs() << "malloc:   " << Process::GetStdMallocUsage() << '\n';
}

template <bool Sign> struct BitData {
  using IntT = H::NBitIntValueType<Sign>;

  const IntT Converted = 0;
  const u64 AllData = 0;
  const unsigned Bits = 0;

public:
  template <unsigned InBits>
  BitData(NBitIntCommon<Sign, InBits> Val) :
   Converted(Val.data()), AllData(exi::bit_cast<u64>(Val)), Bits(InBits) {
  }
};

template <bool Sign, unsigned Bits>
BitData(NBitIntCommon<Sign, Bits>) -> BitData<Sign>;

template <bool DoByteswap = true>
static void printBitIntData(u64 AllData, const char* Pre = nullptr) {
  using Arr = std::array<u8, sizeof(u64)>;
  if (DoByteswap)
    AllData = exi::byteswap(AllData);
  auto A = std::bit_cast<Arr>(AllData);
  fmt::print("  ");
  if (Pre && *Pre)
    fmt::print("{}: ", Pre);
  for (u8 Byte : A) {
    u8 Hi = (Byte >> 4);
    u8 Lo = (Byte & 0xF);
    fmt::print("{:04b}'{:04b} ", Hi, Lo);
  }
  fmt::println("");
}

template <bool DoByteswap = true, bool Sign>
static void printAllData(BitData<Sign> Data, const char* Pre = nullptr) {
  printBitIntData<DoByteswap>(Data.AllData, Pre);
}

static bool commonChecks(
 BitData<1> I,
 BitData<0> U,
 BitData<1> IZero,
 BitData<0> UZero,
 u64 kBits
) {
  fmt::print("#{:02} | ", I.Bits);
  if (I.AllData != U.AllData) {
    fmt::println("Error: IAllData != UAllData.");
    printAllData(I, "I");
    printAllData(U, "U");
    return false;
  }

  if (I.AllData != kBits) {
    fmt::println("Error: AllData != kBits.");
    printBitIntData(kBits, "kBits");
    printAllData(I, "+Data");
    printAllData(U, "~Data");
    printAllData(IZero, "+Zero");
    printAllData(UZero, "~Zero");
    if (I.Converted != -1)
      fmt::println("  Real value: {}", I.Converted);
    return false;
  }

  fmt::println("Success!");
  // printAllData(I, "Data");
  return true;
}

template <unsigned Bits>
static bool testBits() noexcept {
  using IntT = H::NBitIntValueType<false>;
  constexpr usize MAX_BITS = NBitIntBase::kMaxBits;
  constexpr IntT kBits = (~IntT(0) >> (MAX_BITS - Bits));

  using SInt = ibit<Bits>;
  using UInt = ubit<Bits>;

  SInt I = SInt::FromBits(kBits);
  UInt U = UInt::FromBits(kBits);

  // __builtin_clear_padding(&I);
  // __builtin_clear_padding(&U);

  return commonChecks(
    I, U, SInt(0), UInt(0), kBits);
}

static void runAllTests() {
  constexpr usize MAX_BITS = NBitIntBase::kMaxBits;
  bool Result = [] <usize...II>
   (std::index_sequence<II...>) -> bool {
    bool Out = true;
    ((Out &= testBits<II + 1>()), ...);
    return Out;
  } (std::make_index_sequence<MAX_BITS>{});

  fmt::println("");
  if (Result == true) {
    fmt::println("All tests passed!");
  } else {
    fmt::println("Some tests failed.");
    std::exit(1);
  }
}

int main(int Argc, char* Argv[]) {
  exi::DebugFlag = true;

  // miscTests(Argc, Argv);
  u8 Data[] {0b1001'0110, 0b1011'1011, 0b1111'1110};
  InBitStream BS(Data);
  exi_assert(BS.bitPos() == 0, "Yeah.");

  exi_assert(BS.peekBit()     == 1);
  exi_assert(BS.peekBits(4)   == 0b1001);
  exi_assert(BS.readBits<4>() == 0b1001);
  exi_assert(BS.readBits(3)   == 0b011);
  exi_assert(BS.readBit()     == 0);
  exi_assert(BS.peekBits<4>() == 0b1011);
  exi_assert(BS.readBits(4)   == 0b1011);
  exi_assert(BS.readBits(12)  == 0b1011'1111'1110);

  // runAllTests();
  // return 0;

  {
    using SInt = ibit<4>;
    using UInt = ubit<4>;

    SInt I = 0;
    exi_assert(CheckIntCast<u8>(I));
    exi_assert(CheckIntCast<i8>(I));

    exi_assert(!CheckIntCast<SInt>(0b11111));
    I = SInt::FromBits(0b1111);
    UInt U = IntCastOrZero<UInt>(I);
    exi_assert(U == 0);
    U = UInt::FromBits(I);

    outs() << "I: " << I << '\n';
    outs() << "U: " << U << '\n';
  }
  {
    using SInt = ibit<8>;
    using UInt = ubit<5>;

    SInt I = -1;
    exi_assert(CheckIntCast<SInt>(0b11111));
    UInt U = IntCastOrZero<UInt>(I);
    exi_assert(U == 0);

    I = SInt::FromBits(0b11111);
    exi_assert(I == 31);
    U = IntCastOrZero<UInt>(I);
    exi_assert(U == 31);
    U = UInt::FromBits(I);

    using i5 = ibit<5>;
    i5 I2 = i5::FromBits(U);
    i5 I3 = SInt(-1);
    exi_assert(I2 == -1);
    exi_assert(I2 == I3);

    outs() << "I: " << I << '\n';
    outs() << "U: " << U << '\n';
  }
}
