# Redis Module for maintaining hash by simple SQL

This module aims to provide simple DML to manipulate the hashes in REDIS for SQL users. It works as simple as you expected. It translates the input statement to a set of pure REDIS commands. It does not need nor generate any intermediate stuffs which occupied your storages. The target data is your hashes only.

## Example
```bash
$ redis-cli
127.0.0.1:6379> hmset phonebook:0001 name "Peter Nelson"     tel "1-456-1246-3421" birth "2019-10-01" pos 3 gender "M"
127.0.0.1:6379> hmset phonebook:0002 name "Betty Joan"       tel "1-444-9999-1112" birth "2019-12-01" pos 1 gender "F"
127.0.0.1:6379> hmset phonebook:0003 name "Bloody Mary"      tel "1-666-1234-9812" birth "2018-01-31" pos 2 gender "F"
127.0.0.1:6379> hmset phonebook:0004 name "Mattias Swensson" tel "1-888-3333-1412" birth "2017-06-30" pos 4 gender "M"
127.0.0.1:6379> dbx.select name,tel from phonebook where gender = "F" order by pos desc
1) 1) name
   2) "Bloody Mary"
   3) tel
   4) "1-666-1234-9812"
2) 1) name
   2) "Betty Joan"
   3) tel
   4) "1-444-9999-1112"
127.0.0.1:6379> dbx.select * from phonebook where birth > '2019-11-11'
1)  1) "name"
    2) "Betty Joan"
    3) "tel"
    4) "1-444-9999-1112"
    5) "birth"
    6) "2019-12-01"
    7) "pos"
    8) "1"
    9) "gender"
   10) "F"
127.0.0.1:6379> dbx.select tel from phonebook where name like Son
1) 1) tel
   2) "1-888-3333-1412"
2) 1) tel
   2) "1-456-1246-3421"
```

## Getting started

### Get the package and build the binary:
```bash
git clone https://github.com/cscan/dbx.git
cd dbx/src && make
```

This plugin library is written in pure C. A file dbx.so is built after successfully compiled.

### Load the module in redis

Load the module in CLI
```bash
127.0.0.1:6379> module load /path/to/dbx.so
```

Start the server with loadmodule argument
```bash
$ redis-server --loadmodule /path/to/dbx.so
```

Adding the following line in the file redis.conf and then restart the server
```bash
loadmodule /path/to/dbx.so
```

If you still have problem in loading the module, please visit: https://redis.io/topics/modules-intro

## Compatibility
REDIS v4.0

## License
MIT

## Status
Now the Select statement only supports single where condition and single order sequence. I will add more useful features in the future. Simple Insert, Update and Delete statement will be included finally. This project is in an early stage of development. Any contribution is welcome :D
