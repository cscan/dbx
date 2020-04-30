# Redis Module for maintaining hash by simple SQL

## Example
```bash
$ redis-cli
127.0.0.1:6379> hmset phonebook:0001 name "Peter Nelson"     tel "1-456-1246-3421" country "US" effective "2019-10-01" pos 3 flag "Y"
127.0.0.1:6379> hmset phonebook:0002 name "Boris John"       tel "1-444-9999-1112" country "UK" effective "2019-12-01" pos 1 flag "N"
127.0.0.1:6379> hmset phonebook:0003 name "Bloody Mary"      tel "1-666-1234-9812" country "FR" effective "2018-01-31" pos 2 flag "N"
127.0.0.1:6379> hmset phonebook:0004 name "Mattias Swensson" tel "1-888-3333-1412" country "HK" effective "2017-06-30" pos 4 flag "Y"
127.0.0.1:6379> dbx.select name,tel from phonebook where flag = "N" order by pos desc
1) 1) name
   2) "Bloody Mary"
   3) tel
   4) "1-666-1234-9812"
2) 1) name
   2) "Boris John"
   3) tel
   4) "1-444-9999-1112"
127.0.0.1:6379> dbx.select * from phonebook where effective > '2019-11-11'
1)  1) "name"
    2) "Boris John"
    3) "tel"
    4) "1-444-9999-1112"
    5) "country"
    6) "UK"
    7) "effective"
    8) "2019-12-01"
    9) "pos"
   10) "1"
   11) "flag"
   12) "N"
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
```bash
127.0.0.1:6379> module load /path/to/dbx.so
```
OR
Start the server with loadmodule argument
```bash
$ redis-server --loadmodule /path/to/dbx.so
```
OR
Adding the following line in the file redis.conf and then restart the server
```bash
loadmodule /path/to/dbx.so
```

If you still have problem in loading the module, please visit: https://redis.io/topics/modules-intro

## Compatibility
REDIS v4

## License
MIT

## Status
This project is in an early stage of development. Any contribution is welcome :D
