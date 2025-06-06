/*
 * Copyright (c) 2016, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "precompiled.hpp"
#include "gc/shared/collectorPolicy.hpp"
#include "runtime/arguments.hpp"
#include "runtime/flags/flagSetting.hpp"
#include "runtime/globals_extension.hpp"
#include "utilities/align.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"
#include "unittest.hpp"

class TestGenCollectorPolicy {
 public:

  class Executor {
   public:
    virtual void execute() = 0;
  };

  class UnaryExecutor : public Executor {
   protected:
    const size_t param;
   public:
    UnaryExecutor(size_t val) : param(val) { }
  };

  class BinaryExecutor : public Executor {
   protected:
    const size_t param1;
    const size_t param2;
   public:
    BinaryExecutor(size_t val1, size_t val2) : param1(val1), param2(val2) { }
  };

  class MinHeapSizeGuard {
   private:
    const size_t _stored_min_heap_size;
   public:
    MinHeapSizeGuard() : _stored_min_heap_size(Arguments::min_heap_size()) { }
    ~MinHeapSizeGuard() {
      Arguments::set_min_heap_size(_stored_min_heap_size);
    }
  };

  class TestWrapper {
   public:
    static void test(Executor* setter1, Executor* setter2, Executor* checker) {
      FLAG_GUARD(InitialHeapSize);
      FLAG_GUARD(MaxHeapSize);
      FLAG_GUARD(MaxNewSize);
      FLAG_GUARD(MinHeapDeltaBytes);
      FLAG_GUARD(NewSize);
      FLAG_GUARD(OldSize);
      MinHeapSizeGuard min_heap_size_guard;

      FLAG_SET_ERGO(size_t, InitialHeapSize, 100 * M);
      FLAG_SET_ERGO(size_t, OldSize, 4 * M);
      FLAG_SET_ERGO(size_t, NewSize, 1 * M);
      FLAG_SET_ERGO(size_t, MaxNewSize, 50 * M);
      Arguments::set_min_heap_size(40 * M);

      ASSERT_NO_FATAL_FAILURE(setter1->execute());

      if (setter2 != NULL) {
        ASSERT_NO_FATAL_FAILURE(setter2->execute());
      }

      ASSERT_NO_FATAL_FAILURE(checker->execute());
    }
    static void test(Executor* setter, Executor* checker) {
      test(setter, NULL, checker);
    }
  };

  class SetNewSizeErgo : public UnaryExecutor {
   public:
    SetNewSizeErgo(size_t param) : UnaryExecutor(param) { }
    void execute() {
      FLAG_SET_ERGO(size_t, NewSize, param);
    }
  };

  class CheckYoungMin : public UnaryExecutor {
   public:
    CheckYoungMin(size_t param) : UnaryExecutor(param) { }
    void execute() {
      MarkSweepPolicy msp;
      msp.initialize_all();
      ASSERT_LE(msp.min_young_size(), param);
    }
  };

  class CheckScaledYoungInitial : public Executor {
   public:
    void execute() {
      size_t initial_heap_size = InitialHeapSize;
      MarkSweepPolicy msp;
      msp.initialize_all();

      if (InitialHeapSize > initial_heap_size) {
        // InitialHeapSize was adapted by msp.initialize_all, e.g. due to alignment
        // caused by 64K page size.
        initial_heap_size = InitialHeapSize;
      }

      size_t expected = msp.scale_by_NewRatio_aligned(initial_heap_size);
      ASSERT_EQ(expected, msp.initial_young_size());
      ASSERT_EQ(expected, NewSize);
    }
  };

  class SetNewSizeCmd : public UnaryExecutor {
   public:
    SetNewSizeCmd(size_t param) : UnaryExecutor(param) { }
    void execute() {
      FLAG_SET_CMDLINE(size_t, NewSize, param);
    }
  };

  class CheckYoungInitial : public UnaryExecutor {
   public:
    CheckYoungInitial(size_t param) : UnaryExecutor(param) { }
    void execute() {
      MarkSweepPolicy msp;
      msp.initialize_all();

      ASSERT_EQ(param, msp.initial_young_size());
    }
  };

  class SetOldSizeCmd : public UnaryExecutor {
   public:
    SetOldSizeCmd(size_t param) : UnaryExecutor(param) { }
    void execute() {
      FLAG_SET_CMDLINE(size_t, OldSize, param);
    }
  };

  class SetMaxNewSizeCmd : public BinaryExecutor {
   public:
    SetMaxNewSizeCmd(size_t param1, size_t param2) : BinaryExecutor(param1, param2) { }
    void execute() {
      size_t heap_alignment = CollectorPolicy::compute_heap_alignment();
      size_t new_size_value = align_up(MaxHeapSize, heap_alignment)
              - param1 + param2;
      FLAG_SET_CMDLINE(size_t, MaxNewSize, new_size_value);
    }
  };

  class CheckOldMin : public UnaryExecutor {
   public:
    CheckOldMin(size_t param) : UnaryExecutor(param) { }
    void execute() {
      MarkSweepPolicy msp;
      msp.initialize_all();
      ASSERT_LE(msp.min_old_size(), param);
    }
  };

  class CheckOldInitial : public Executor {
   public:
    void execute() {
      size_t heap_alignment = CollectorPolicy::compute_heap_alignment();

      MarkSweepPolicy msp;
      msp.initialize_all();

      size_t expected_old_initial = align_up(InitialHeapSize, heap_alignment)
              - MaxNewSize;

      ASSERT_EQ(expected_old_initial, msp.initial_old_size());
    }
  };

  class CheckOldInitialMaxNewSize : public BinaryExecutor {
   public:
    CheckOldInitialMaxNewSize(size_t param1, size_t param2) : BinaryExecutor(param1, param2) { }
    void execute() {
      size_t heap_alignment = CollectorPolicy::compute_heap_alignment();
      size_t new_size_value = align_up(MaxHeapSize, heap_alignment)
              - param1 + param2;

      MarkSweepPolicy msp;
      msp.initialize_all();

      size_t expected_old_initial = align_up(MaxHeapSize, heap_alignment)
              - new_size_value;

      ASSERT_EQ(expected_old_initial, msp.initial_old_size());
    }
  };
};


// Testing that the NewSize flag is handled correct is hard because it
// depends on so many other configurable variables. These tests only try to
// verify that there are some basic rules for NewSize honored by the policies.

// If NewSize has been ergonomically set, the collector policy
// should use it for min
TEST_VM(CollectorPolicy, young_min_ergo) {
  TestGenCollectorPolicy::SetNewSizeErgo setter(20 * M);
  TestGenCollectorPolicy::CheckYoungMin checker(20 * M);

  TestGenCollectorPolicy::TestWrapper::test(&setter, &checker);
}

// Tests require at least 128M of MaxHeap
// otherwise ergonomic is different and generation sizes might be changed.

// If NewSize has been ergonomically set, the collector policy
// should use it for min but calculate the initial young size
// using NewRatio.
TEST_VM(CollectorPolicy, young_scaled_initial_ergo) {
  if (MaxHeapSize < 128 * M) {
      return;
  }
  TestGenCollectorPolicy::SetNewSizeErgo setter(20 * M);
  TestGenCollectorPolicy::CheckScaledYoungInitial checker;

  TestGenCollectorPolicy::TestWrapper::test(&setter, &checker);
}


// Since a flag has been set with FLAG_SET_CMDLINE it
// will be treated as it have been set on the command line for
// the rest of the VM lifetime. This is an irreversible change and
// could impact other tests so we use TEST_OTHER_VM
TEST_OTHER_VM(CollectorPolicy, young_cmd) {
  if (MaxHeapSize < 128 * M) {
    return;
  }
  // If NewSize is set on the command line, it should be used
  // for both min and initial young size if less than min heap.
  TestGenCollectorPolicy::SetNewSizeCmd setter(20 * M);

  TestGenCollectorPolicy::CheckYoungMin checker_min(20 * M);
  TestGenCollectorPolicy::TestWrapper::test(&setter, &checker_min);

  TestGenCollectorPolicy::CheckYoungInitial checker_initial(20 * M);
  TestGenCollectorPolicy::TestWrapper::test(&setter, &checker_initial);

  // If NewSize is set on command line, but is larger than the min
  // heap size, it should only be used for initial young size.
  TestGenCollectorPolicy::SetNewSizeCmd setter_large(50 * M);
  TestGenCollectorPolicy::CheckYoungInitial checker_large(50 * M);
  TestGenCollectorPolicy::TestWrapper::test(&setter_large, &checker_large);
}

// Since a flag has been set with FLAG_SET_CMDLINE it
// will be treated as it have been set on the command line for
// the rest of the VM lifetime. This is an irreversible change and
// could impact other tests so we use TEST_OTHER_VM
TEST_OTHER_VM(CollectorPolicy, old_cmd) {
  // If OldSize is set on the command line, it should be used
  // for both min and initial old size if less than min heap.
  TestGenCollectorPolicy::SetOldSizeCmd setter(20 * M);

  TestGenCollectorPolicy::CheckOldMin checker_min(20 * M);
  TestGenCollectorPolicy::TestWrapper::test(&setter, &checker_min);

  TestGenCollectorPolicy::CheckOldInitial checker_initial;
  TestGenCollectorPolicy::TestWrapper::test(&setter, &checker_initial);

  // If MaxNewSize is large, the maximum OldSize will be less than
  // what's requested on the command line and it should be reset
  // ergonomically.
  // We intentionally set MaxNewSize + OldSize > MaxHeapSize
  TestGenCollectorPolicy::SetOldSizeCmd setter_old_size(30 * M);
  TestGenCollectorPolicy::SetMaxNewSizeCmd setter_max_new_size(30 * M, 20 * M);
  TestGenCollectorPolicy::CheckOldInitialMaxNewSize checker_large(30 * M, 20 * M);

  TestGenCollectorPolicy::TestWrapper::test(&setter_old_size, &setter_max_new_size, &checker_large);
}
