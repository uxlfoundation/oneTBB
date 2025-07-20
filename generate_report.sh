#!/bin/bash

BUILD_DIR=$(find build -maxdepth 1 -type d -name "gnu_*" | head -n 1)
if [[ -z "$BUILD_DIR" ]]; then
    echo "Error: No gnu_* directory found inside build/"
    exit 1
fi

# Define the executable and output file
EXE_NAME=$1

TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
OUTPUT_FILE="${BUILD_DIR}/${EXE_NAME}_execution_report_${TIMESTAMP}.txt" #Output files in build/gnu_*/

# List of n-of-iterations values to test
ITERATIONS=(10 20 30 40 50 100 200 300 500) 

# Clear previous report
# rm -rf $OUTPUT_FILE
rm -rf ${BUILD_DIR}/${EXE_NAME}_iterations_*.txt

echo "EXAMPLE Name | Number of Iterations | Relative_Err" > $OUTPUT_FILE
echo "--------------------------------------------------" >> $OUTPUT_FILE

# Loop through each iteration value
for n in "${ITERATIONS[@]}"; do
    echo "Running $EXE_NAME with n-of-iterations=$n"

    TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
   
    LOG_FILE="${BUILD_DIR}/${EXE_NAME}_iterations_${n}_${TIMESTAMP}.txt" 

    # Run the executable and capture output
    OUTPUT=$(${BUILD_DIR}/$EXE_NAME n-of-iterations=$n)

    wait
   

    # Extract Relative_Err (modify grep or awk based on actual output format)
    REL_ERR=$(echo "$OUTPUT" | grep "Relative_Err" | awk -F': ' '{print $2}' | tr -d ' %')

    echo "$REL_ERROR" 

    echo "$OUTPUT" > "$LOG_FILE"

    # Append to the report
    echo "$EXE_NAME | $n | $REL_ERR" >> $OUTPUT_FILE
done

echo "Report generated: $OUTPUT_FILE"

