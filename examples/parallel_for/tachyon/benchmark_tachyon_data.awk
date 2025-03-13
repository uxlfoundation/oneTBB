BEGIN {
        print "infile,repetitions,avg_duration,rel_error"
        summ=0;
        cnt=0;
}

match($0, /tachyon.*n-of-repeats=([0-9]+) (.+): ([.0-9]+) seconds/, arr)  {
  curr_rep = arr[1];
  curr_file = arr[2];
  summ += arr[3];
  cnt++
}

/Relative_Err/ {
  print curr_file "," curr_rep "," summ/cnt "," $3
}
