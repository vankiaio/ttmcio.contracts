#!/bin/bash
#
first_stamp=`date -d "2019-02-21 19:36:00" +%s` #计算指定日期的时间戳
second_stamp=`date -d "2019-02-21 19:37:00" +%s` #计算指定日期的时间戳
third_stamp=`date -d "2019-02-21 19:38:00" +%s` #计算指定日期的时间戳
today_stamp=`date +%s`                          #计算当天的时间戳
let day1_stamp=($first_stamp - $today_stamp - 1)     #当天的时间戳减去指定的时间戳
let day2_stamp=($second_stamp - $today_stamp - 2)     #当天的时间戳减去指定的时间戳
let day3_stamp=($third_stamp - $today_stamp - 2)     #当天的时间戳减去指定的时间戳
let day=($day1_stamp/86400)                      #相差的时间戳除以一天的秒数就得到天数
echo $day
echo $day1_stamp
echo $day2_stamp
echo $day3_stamp
cmd1=$(cleos --wallet-url http://127.0.0.1:8900 --url http://119.23.146.214:8888 push action eosio.token issuelock '["'$1'", "'$2' TTMC", "memo","'$3' TTMC", "'$day1_stamp'"]' -p eosio)
cmd2=$(cleos --wallet-url http://127.0.0.1:8900 --url http://119.23.146.214:8888 push action eosio.token issuelock '["'$1'", "'$2' TTMC", "memo","'$3' TTMC", "'$day2_stamp'"]' -p eosio)
cmd3=$(cleos --wallet-url http://127.0.0.1:8900 --url http://119.23.146.214:8888 push action eosio.token issuelock '["'$1'", "'$2' TTMC", "memo","'$3' TTMC", "'$day3_stamp'"]' -p eosio)
echo $cmd1
echo $cmd2
echo $cmd3

