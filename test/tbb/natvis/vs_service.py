import win32com.client
import time
import logging
import os

class VsService:
    def __init__(self, file_path):
        self.file_path = file_path
        self.DEBUG_MODE = True
        # Configure logging
        if self.DEBUG_MODE:
            logging.basicConfig(filename='natvis_output.log', level=logging.INFO,
                                format='%(asctime)s - %(levelname)s - %(message)s')
        else:
            logging.basicConfig(filename='natvis_output.log', level=logging.WARNING,
                                format='%(asctime)s - %(levelname)s - %(message)s')

    def open_visual_studio(self, solution_path):
        try:
            # Create or connect to a running Visual Studio instance
            self.dte = win32com.client.Dispatch("VisualStudio.DTE")  
            self.dte.MainWindow.Visible = True  # Ensure UI is visible
            if os.path.exists(solution_path):
                logging.info(f"Opening solution: {solution_path}")
                self.dte.Solution.Open(solution_path)
                time.sleep(5)  # Give VS some time to load the solution
            else:
                logging.error("Solution file not found.")
                return None
            
            return self.dte
        except Exception as e:
            logging.error(f"Error opening Visual Studio: {e}")
            return None

    def add_breakpoint(self, line_number):
        """Add a breakpoint at the specified file and line number."""
        logging.info(f"Adding breakpoint at {self.file_path}:{line_number}...")

        try:
            self.dte.Debugger.Breakpoints.Add("", self.file_path, line_number)
            logging.info("Breakpoint added successfully.")
        except Exception as e:
            logging.error(f"Error adding breakpoint: {e}")

    def add_watch_variable(self, variable_name):
        logging.info(f"Entering  add_watch_variable : {variable_name}")
        try:
            if self.dte is None:
                logging.error("No active Visual Studio instance found.")
                return None
            
            debugger = self.dte.Debugger

            # Ensure debugging is active before adding a watch
            if debugger.CurrentMode != 1:  # Mode 1 = Running, 2 = Break Mode
                logging.info("Starting debugging session...")
                debugger.Go(wait_for_break_or_end=True)
                time.sleep(300)  # Wait for debugging to start
                logging.info(f"Debugger activated")
            debugger.Go()
            time.sleep(30)  # Wait for debugging to start


            # Get the Output window
            output_window = self.dte.Windows.Item("Output")
        
            # Get the Output window's text
            output_pane = output_window.Object.OutputWindowPanes.Item("Debug")
            output_text = output_pane.TextDocument.StartPoint.CreateEditPoint().GetText(output_pane.TextDocument.EndPoint)
            if "Error" in output_text:
                print("Error : Natvis File Incorrect")
            logging.info("Output logs have been written to vs_output_logs.txt")
            logging.info(output_text)
            time.sleep(20)
            logging.info(f"Adding watch variable: {variable_name}")
            # Add the variable to the watch window
            debugger.WatchExpressions.Add(variable_name)

            logging.info(f"Added watch variable: {variable_name}")
        except Exception as e:
            logging.error(f"Error adding watch variable: {e}")
            self.dte.Solution.Close()
            self.dte.Quit()
            return None
        # Close the solution
        self.dte.Solution.Close()
        self.dte.Quit()
        logging.info("Solution closed successfully.")