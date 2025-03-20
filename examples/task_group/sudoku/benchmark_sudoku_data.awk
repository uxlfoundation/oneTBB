BEGIN {
        print "infile,repetitions,threads,avg_duration,rel_error"
        match(run_args, /filename=(.+)[ ;]/, run_args_);
        curr_file = run_args_[1]
}

match($0, /(Sudoku: Time to find .* on )([0-9]+)( threads: )([.0-9]+) seconds./, arr) {
  t_id=arr[2];
  thread_sum[t_id] += arr[4];
  thread_cnt[t_id] = thread_cnt[t_id] + 1
}

match($0, /(Sudoku: Time to find.* on )([0-9]+)( threads[:]* Relative_Err : )([.0-9]+) %/, arr) {
  t_id=arr[2];
  print curr_file "," thread_cnt[t_id] "," t_id "," thread_sum[t_id]/(thread_cnt[t_id]) "," arr[4];
}
