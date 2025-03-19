# Copyright (C) 2005-2011, Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
#
#
.NOTPARALLEL:
default:
	@-echo Usage: gmake [test_{name}][debug_{name}][clean]

tbb_root=../..
tbb_strict=0
BUILDING_PHASE=0
include $(tbb_root)/build/common.inc

VPATH = $(tbb_root)/src/tbb/$(ASSEMBLY_SOURCE) $(tbb_root)/src/tbb $(tbb_root)/src/test $(tbb_root)/include/tbb $(work_dir)_$(cfg)
include $(tbb_root)/build/common_rules.inc
CPLUS_FLAGS += $(INCLUDE_KEY) $(tbb_root)/src $(INCLUDE_KEY) $(tbb_root)/include/tbb $(INCLUDE_KEY) $(tbb_root)/src/test $(INCLUDE_KEY) $(tbb_root)/src/tbb -L $(work_dir)_$(cfg)
DEBUG_SUFFIX=$(findstring _debug,_$(cfg))
TEST_PREREQUISITE+=$(TBB.LIB)
LINK_FILES+=-ltbb

ifeq ($(tbb_os),windows)
    export PATH := $(work_dir)_$(cfg);$(PATH)
else
    ifeq ($(tbb_os),macos)
        export DYLD_LIBRARY_PATH := $(work_dir)_$(cfg):$(DYLD_LIBRARY_PATH)
    else
        export LD_LIBRARY_PATH := $(work_dir)_$(cfg):$(LD_LIBRARY_PATH)
    endif
endif

#tbb_root = $(subst source$(SLASH)test,,$(CWD))tbb/1.0
#TARGETS = $(subst .cpp,.exe,$(wildcard test_*.cpp time_*.cpp))
#.PHONY: clean tbb tbb_release tbb_debug

tbb: tbb_release tbb_debug
tbb_%::
	$(MAKE) -C $(tbb_root)/src $@

bench_%:
	g++ bench_accessing.cpp -I../../include -lbenchmark -fopenmp -std=c++17 -o$@
	./$@ $(args)

test_%: test_%.exe
	./$^ $(args)

debug_%: test_%.exe
	$(debugger) ./$^ $(debug_args)

dtime_%: time_%.exe
	$(debugger) ./$^ $(debug_args)

clean:
	$(RM) *.$(OBJ) *.exe *.$(DLL) *.$(LIBEXT) *.res *.map *.ilk *.pdb *.exp *.*manifest *.tmp *.d
	
#$(TARGETS)::
#	-$(MAKE) -C $(@D)  -f $(EXAMPLE_MAKEFILE) $(subst .,,$(suffix $@)) CXX="$(CPLUS)"

# Include automatically generated dependences
#-include *.d
