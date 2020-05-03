# Redis Module for maintaining hash by simple SQL (Support csv import/export)

This module aims to provide simple DML to manipulate the hashes in REDIS for SQL users. It works as simple as you expected. It translates the input statement to a set of pure REDIS commands. It does not need nor generate any intermediate stuffs which occupied your storages. The target data is your hashes only. It also provides the CSV import and export function.

## Usage
```sql
$ redis-cli
127.0.0.1:6379> hmset phonebook:0001 name "Peter Nelson"     tel "1-456-1246-3421" birth "2019-10-01" pos 3 gender "M"
127.0.0.1:6379> hmset phonebook:0002 name "Betty Joan"       tel "1-444-9999-1112" birth "2019-12-01" pos 1 gender "F"
127.0.0.1:6379> hmset phonebook:0003 name "Bloody Mary"      tel "1-666-1234-9812" birth "2018-01-31" pos 2 gender "F"
127.0.0.1:6379> hmset phonebook:0004 name "Mattias Swensson" tel "1-888-3333-1412" birth "2017-06-30" pos 4 gender "M"
127.0.0.1:6379> dbx select name,tel from phonebook where gender = "F" order by pos desc
1) 1) name
   2) "Bloody Mary"
   3) tel
   4) "1-666-1234-9812"
2) 1) name
   2) "Betty Joan"
   3) tel
   4) "1-444-9999-1112"
```

## Getting started

### Get the package and build the binary:
```sql
$ git clone https://github.com/cscan/dbx.git
$ cd dbx/src && make
```

This plugin library is written in pure C. A file dbx.so is built after successfully compiled.

### Load the module in redis (3 ways)

1. Load the module in CLI
```sql
127.0.0.1:6379> module load /path/to/dbx.so
```

2. Start the server with loadmodule argument
```sql
$ redis-server --loadmodule /path/to/dbx.so
```

3. Adding the following line in the file redis.conf and then restart the server
```sql
loadmodule /path/to/dbx.so
```

If you still have problem in loading the module, please visit: https://redis.io/topics/modules-intro

## More Examples

### Select statement
You may specify multiple fields separated by comma
```sql
127.0.0.1:6379> dbx select name, gender, birth from phonebook
1) 1) name
   2) "Betty Joan"
   3) gender
   4) "F"
   5) birth
   6) "2019-12-01"
2) 1) name
   2) "Mattias Swensson"
   3) gender
   4) "M"
   5) birth
   6) "2017-06-30"
3) 1) name
   2) "Peter Nelson"
   3) gender
   4) "M"
   5) birth
   6) "2019-10-01"
4) 1) name
   2) "Bloody Mary"
   3) gender
   4) "F"
   5) birth
   6) "2018-01-31"
```

"*" is support
```sql
127.0.0.1:6379> dbx select * from phonebook where birth > '2019-11-11'
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
```

If you want to show the exact keys, you may try rowid()
```sql
127.0.0.1:6379> dbx select rowid() from phonebook
1) 1) rowid()
   2) "phonebook:1588299191-764848276"
2) 1) rowid()
   2) "phonebook:1588299202-1052597574"
3) 1) rowid()
   2) "phonebook:1588298418-551514504"
4) 1) rowid()
   2) "phonebook:1588299196-2115347437"
```

The above is nearly like REDIS keys command
```sql
127.0.0.1:6379> keys phonebook*
1) "phonebook:1588298418-551514504"
2) "phonebook:1588299196-2115347437"
3) "phonebook:1588299202-1052597574"
4) "phonebook:1588299191-764848276"
```

Each record is exactly a hash, you could use raw REDIS commands ``hget, hmget or hgetall`` to retrieve the same content

#### Where clause
Your could specify =, >, <, >=, <=, <>, != or like conditions in where clause. Now the module only support "and" to join multiple conditions.
```sql
127.0.0.1:6379> dbx select tel from phonebook where name like Son
1) 1) tel
   2) "1-888-3333-1412"
2) 1) tel
   2) "1-456-1246-3421"
127.0.0.1:6379> dbx select tel from phonebook where name like Son and pos = 4
1) 1) tel
   2) "1-888-3333-1412"
```

#### Order clause
Ordering can be ascending or descending. All sortings are alpha-sort.
```sql
127.0.0.1:6379> dbx select name, pos from phonebook order by pos asc
1) 1) name
   2) "Betty Joan"
   3) pos
   4) "1"
2) 1) name
   2) "Bloody Mary"
   3) pos
   4) "2"
3) 1) name
   2) "Peter Nelson"
   3) pos
   4) "3"
4) 1) name
   2) "Mattias Swensson"
   3) pos
   4) "4"
127.0.0.1:6379> dbx select name from phonebook order by pos desc
1) 1) name
   2) "Mattias Swensson"
2) 1) name
   2) "Peter Nelson"
3) 1) name
   2) "Bloody Mary"
4) 1) name
   2) "Betty Joan"
```

#### Top clause
```sql
127.0.0.1:6379> dbx select top 3 name, tel from phonebook order by pos desc
1) 1) name
   2) "Mattias Swensson"
   3) tel
   4) "1-888-3333-1412"
2) 1) name
   2) "Peter Nelson"
   3) tel
   4) "1-456-1246-3421"
3) 1) name
   2) "Bloody Mary"
   3) tel
   4) "1-666-1234-9812"
127.0.0.1:6379> dbx select top 0 * from phonebook
(empty list or set)
```

#### Into clause for copy hash table
You could create another hash table by into clause.
```sql
127.0.0.1:6379> dbx select * into testbook from phonebook
1) testbook:1588325407-1751904058
2) testbook:1588325407-1751904059
3) testbook:1588325407-1751904060
4) testbook:1588325407-1751904061
127.0.0.1:6379> keys testbook*
1) "testbook:1588325407-1751904061"
2) "testbook:1588325407-1751904059"
3) "testbook:1588325407-1751904058"
4) "testbook:1588325407-1751904060"
127.0.0.1:6379> dbx select * from testbook
1)  1) "name"
    2) "Mattias Swensson"
    3) "tel"
    4) "1-888-3333-1412"
    5) "birth"
    6) "2017-06-30"
    7) "pos"
    8) "4"
    9) "gender"
   10) "M"
2)  1) "name"
    2) "Peter Nelson"
    3) "tel"
    4) "1-456-1246-3421"
    5) "birth"
    6) "2019-10-01"
    7) "pos"
    8) "3"
    9) "gender"
   10) "M"
3)  1) "name"
    2) "Bloody Mary"
    3) "tel"
    4) "1-666-1234-9812"
    5) "birth"
    6) "2018-01-31"
    7) "pos"
    8) "2"
    9) "gender"
   10) "F"
4)  1) "name"
    2) "Betty Joan"
    3) "tel"
    4) "1-444-9999-1112"
    5) "birth"
    6) "2019-12-01"
    7) "pos"
    8) "1"
    9) "gender"
   10) "F"
```

#### Into csv clause for exporting records in csv format
```sql
127.0.0.1:6379> dbx select * into csv "/tmp/testbook.csv" from phonebook where pos > 2
1) Kevin Louis,111-2123-1233,2009-12-31,6,F
2) Kenneth Cheng,123-12134-123,2000-12-31,5,M
127.0.0.1:6379> quit
$ cat /tmp/testbook.csv
Kevin Louis,111-2123-1233,2009-12-31,6,F
Kenneth Cheng,123-12134-123,2000-12-31,5,M
$
```

### Delete statement
You may also use Insert and Delete statement to operate the hash. If you does not provide the where clause, it will delete all the records of the specified key prefix. (i.e. phonebook)
```sql
127.0.0.1:6379> dbx delete from phonebook where gender = F
(integer) 2
127.0.0.1:6379> dbx delete from phonebook
(integer) 2
```

### Insert statement
The module provides simple Insert statement which same as the function of the REDIS command hmset. It will append a random string to your provided key (i.e. phonebook). If operation is successful, it will return the key name.
```sql
127.0.0.1:6379> dbx insert into phonebook (name,tel,birth,pos,gender) values ('Peter Nelson'     ,1-456-1246-3421, 2019-10-01, 3, M)
"phonebook:1588298418-551514504"
127.0.0.1:6379> dbx insert into phonebook (name,tel,birth,pos,gender) values ('Betty Joan'       ,1-444-9999-1112, 2019-12-01, 1, F)
"phonebook:1588299191-764848276"
127.0.0.1:6379> dbx insert into phonebook (name,tel,birth,pos,gender) values ('Bloody Mary'      ,1-666-1234-9812, 2018-01-31, 2, F)
"phonebook:1588299196-2115347437"
127.0.0.1:6379> dbx insert into phonebook (name,tel,birth,pos,gender) values ('Mattias Swensson' ,1-888-3333-1412, 2017-06-30, 4, M)
"phonebook:1588299202-1052597574"
127.0.0.1:6379> hgetall phonebook:1588298418-551514504
 1) "name"
 2) "Peter Nelson"
 3) "tel"
 4) "1-456-1246-3421"
 5) "birth"
 6) "2019-10-01"
 7) "pos"
 8) "3"
 9) "gender"
10) "M"
127.0.0.1:6379>
```
Note that Redis requires at least one space after the single and double quoted arguments, otherwise you will get ``Invalid argument(s)`` error. If you don't want to take care of this, you could quote the whole SQL statement by double quote as below:
```sql
127.0.0.1:6379> dbx "insert into phonebook (name,tel,birth,pos,gender) values ('Peter Nelson','1-456-1246-3421','2019-10-01',3, 'M')"
```

#### From clause for importing CSV file
The module provides simple import function by specifying from clause in Insert statement. It only support comma deliminated. Please make sure that the specified import file can be accessed by Redis server.
```bash
$ cat > /tmp/test.csv << EOF
"Kenneth Cheng","123-12134-123","2000-12-31","5","M"
"Kevin Louis","111-2123-1233","2009-12-31","6","F"
EOF
$ redis-cli
127.0.0.1:6379> dbx insert into phonebook (name, tel, birth, pos, gender) from "/tmp/test.csv"
1) "phonebook:1588509697-1579004777"
2) "phonebook:1588509697-1579004778"
127.0.0.1:6379> dbx select name from phonebook
1) 1) name
   2) "Kenneth Cheng"
2) 1) name
   2) "Kevin Louis"
127.0.0.1:6379> dbx delete from phonebook
(integer) 2
127.0.0.1:6379> quit
$ cat > /tmp/testheader.csv << EOF
name,tel,birth,pos,gender
"Kenneth Cheng","123-12134-123","2000-12-31","5","M"
"Kevin Louis","111-2123-1233","2009-12-31","6","F"
EOF
$ redis-cli
127.0.0.1:6379> dbx insert into phonebook from "/tmp/testheader.csv"
1) "phonebook:1588509697-1579004779"
2) "phonebook:1588509697-1579004780"
127.0.0.1:6379> dbx select name from phonebook
1) 1) name
   2) "Kenneth Cheng"
2) 1) name
   2) "Kevin Louis"
```

### Issue command from BASH shell
```sql
$ redis-cli dbx select "*" from phonebook where gender = M order by pos desc
1)  1) "name"
    2) "Mattias Swensson"
    3) "tel"
    4) "1-888-3333-1412"
    5) "birth"
    6) "2017-06-30"
    7) "pos"
    8) "4"
    9) "gender"
   10) "M"
2)  1) "name"
    2) "Peter Nelson"
    3) "tel"
    4) "1-456-1246-3421"
    5) "birth"
    6) "2019-10-01"
    7) "pos"
    8) "3"
    9) "gender"
   10) "M"
$ redis-cli dbx select name from phonebook where tel like 9812
1) 1) name
   2) "Bloody Mary"
```
Note that "*" requires double quoted otherwise it will pass all the filename in current directory. Of course you could quote the whole SQL statement.
```sql
$ redis-cli dbx "select * from phonebook where gender = M order by pos desc"
```

## Compatibility
REDIS v4.0

## License
MIT

## Status
This project is in an early stage of development. Any contribution is welcome :D
