# Copyright (c) 2025 Intel Corporation
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

from vs_service import VsService
import os
import argparse

if __name__ == "__main__":
    # Set up argument parser
    parser = argparse.ArgumentParser(description='Set TBBROOT and launch Visual Studio project.')
    parser.add_argument('TBBROOT', type=str, help='Path to the TBB installation')
    args = parser.parse_args()
    # Set the TBBROOT environment variable
    os.environ['TBBROOT'] = args.TBBROOT
    dirname = os.path.dirname(__file__)
    solution = os.path.join(dirname, 'natvis.sln')
    watch_variable = "concVec"
    file_path = os.path.join(dirname, 'main.cpp')
    line_number = 33  # Change to the line where you want the breakpoint
    vs = VsService(file_path)

    # Open the solution and attach to Visual Studio
    dte_instance = vs.open_visual_studio(solution)

    if dte_instance:
        # Add watch variable after ensuring debugging is running
        vs.add_breakpoint(line_number)
        vs.add_watch_variable(watch_variable)