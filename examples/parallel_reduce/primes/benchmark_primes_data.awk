BEGIN {
        print "range,repetitions,parallelism,avg_duration,rel_error"
}

match($0, /^#primes from \[([.0-9]+)\] .*\(([.0-9]+) sec with (.+) /, arr) {
  t_id=arr[3];
  range[t_id] = arr[1]
  thread_sum[t_id] += arr[2];
  thread_cnt[t_id] = thread_cnt[t_id] + 1
}

match($0, /(.+) (.+) Relative_Err : ([.0-9]+) %/, arr) {
  t_id=arr[1];
  print range[t_id] "," thread_cnt[t_id] "," t_id "," thread_sum[t_id]/(thread_cnt[t_id]) "," arr[3];
}
