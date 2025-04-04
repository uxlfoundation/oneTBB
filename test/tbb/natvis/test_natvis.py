from vs_service import VsService
import os

if __name__ == "__main__":
    dirname = os.path.dirname(__file__)
    solution = os.path.join(dirname, 'natvis.sln')
    watch_variable = "concVec"    # use Natvis
    #file_path = r"C:\wa\TBB-Natvis\natvis\main.cpp"  # Change to your source file
    file_path = os.path.join(dirname, 'main.cpp')
    line_number = 33  # Change to the line where you want the breakpoint
    vs = VsService(file_path)

    # Open the solution and attach to Visual Studio
    dte_instance = vs.open_visual_studio(solution)

    if dte_instance:
        # Add watch variable after ensuring debugging is running
        vs.add_breakpoint(line_number)
        vs.add_watch_variable(watch_variable)