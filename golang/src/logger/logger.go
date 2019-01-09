package logger

import (
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"
)

var (
	level      = 1
	backup_cnt = 30
	stderr     = log.New(os.Stderr, "", log.LstdFlags)
	last_date  int

	log_chan = make(chan string, 1024)
	log_file *os.File

	log_dir, log_name string
	app_log           *log.Logger
)

func init() {
	log_dir = "logs"
	_, err := os.Stat(log_dir)
	if os.IsNotExist(err) { // create log dir if not exist
		err = os.Mkdir(log_dir, os.ModePerm)
		if nil != err {
			stderr.Output(2, err.Error())
		}
	}

	log_name = os.Args[0]
	last_slash := strings.LastIndex(log_name, "/")
	if -1 != last_slash {
		log_name = log_name[last_slash+1:]
	}
	log_name = log_dir + "/" + log_name + ".log"

	log_file, err = os.OpenFile(log_name, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0644)
	if nil != err {
		stderr.Output(2, err.Error())
	}

	_, err = log_file.Seek(0, io.SeekStart)
	if nil != err {
		stderr.Output(2, err.Error())
	}

	date_buf := make([]byte, 10)
	log_file.Read(date_buf)
	if 0 == date_buf[0] {
		now := time.Now()
		last_date = now.Year()*10000 + int(now.Month())*100 + now.Day()
	} else {
		date_arr := strings.Split(string(date_buf), "/")
		if 3 != len(date_arr) {
			stderr.Fatalf("invalid date_str=%v in log_file=%v", string(date_buf), log_name)
		}

		year, _ := strconv.Atoi(date_arr[0])
		month, _ := strconv.Atoi(date_arr[1])
		day, _ := strconv.Atoi(date_arr[2])
		last_date = year*10000 + month*100 + day
	}

	app_log = log.New(log_file, "", 0)
	go log_loop()
}

func log_loop() {
	for {
		date_time := time.Now()
		curr_date := date_time.Year()*10000 + int(date_time.Month())*100 + date_time.Day()
		if curr_date != last_date { // rotate log
			rotate_log()
			last_date = curr_date
		}

		str := <-log_chan
		if '\n' != str[len(str)-1] {
			str += "\n"
		}
		time_str := fmt.Sprintf("%04d/%02d/%02d %02d:%02d:%02d %s", date_time.Year(), int(date_time.Month()),
			date_time.Day(), date_time.Hour(), date_time.Minute(), date_time.Second(), str)
		app_log.Output(2, time_str)

		if "FATAL" == str[1:6] {
			stderr.Fatal(str)
		}
	}
}

func rotate_log() {
	log_file.Close() // close log file
	suffix_str := fmt.Sprintf("%04d-%02d-%02d", last_date/10000, (last_date%10000)/100, last_date%100)
	new_log_name := log_name + "." + suffix_str
	err := os.Rename(log_name, new_log_name)
	if nil != err {
		stderr.Fatalf("rename log file when rotate log failed: %v", err)
	}

	log_files := make([]string, 0)
	files, _ := ioutil.ReadDir(log_dir) // get log files
	for _, file := range files {
		if !file.IsDir() {
			log_files = append(log_files, file.Name())
		}
	}

	if len(log_files) > backup_cnt { // remove old files
		sort.Strings(log_files)
		remove_cnt := len(log_files) - backup_cnt
		for i := 0; i < remove_cnt; i++ {
			os.Remove(log_dir + "/" + log_files[i])
		}
	}

	log_file, err = os.OpenFile(log_name, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0644)
	if nil != err {
		stderr.Fatalf("open log file when rotate log failed: %v", err)
	}
	app_log.SetOutput(log_file)
}

func SetLogger(lvl, back int) {
	level = lvl
	backup_cnt = back
}

func Debug(format string, a ...interface{}) {
	if level > 0 {
		return
	}
	str := "[DEBUG] " + fmt.Sprintf(format, a...)
	log_chan <- str
}

func Info(format string, a ...interface{}) {
	if level > 1 {
		return
	}
	str := "[INFO] " + fmt.Sprintf(format, a...)
	log_chan <- str
}

func Warn(format string, a ...interface{}) {
	if level > 2 {
		return
	}
	str := "[WARN] " + fmt.Sprintf(format, a...)
	log_chan <- str
}

func Error(format string, a ...interface{}) {
	if level > 3 {
		return
	}
	str := "[ERROR] " + fmt.Sprintf(format, a...)
	log_chan <- str
}

func Fatal(format string, a ...interface{}) {
	if level > 4 {
		return
	}
	str := "[FATAL] " + fmt.Sprintf(format, a...)
	log_chan <- str
}
