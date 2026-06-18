# Lab Tasks

## SQLite3

* **Page Size & Page Count :** Without any external changes , or by default , the observed page_size was 4096 and page_count was 2.
command : 
`PRAGMA page_size; 
PRAGMA page_count;`

`Query : Q = select * from data where desc_ like '%abc%';` _table data has 1000000 rows_
* **Query Performance :** Without any pre-stored cache before execution , the execution time (`.timer on` command for observing the exec time) for the Query Q was 10.175565. During this execution the mmap_size was 0.

* **MMAP Impact :** The execution time for query Q after `PRAGMA mmap_size=256000000;` recorded to be 9.360013 , as the process was directly able to look into the shared page cache.


## PostgreSQL

* **Block Size & Block Count :** Without any external changes , or by default , the observed block_size was 8192 and number of blocks was 0.
command : 
`SHOW block_size; 
SELECT relpages FROM pg_class WHERE relname = 'data';`

`Query : Q = select * from data where desc_ like '%abc%';` _table data has 1000000 rows_
* **Query Performance :** Without any pre-stored cache in the `shared buffers` before execution , the execution time (`explain analyze` command for observing the exec time) for the Query Q was 12.450. During this execution the no blocks were stored in the `shared buffers` pre-query.

* **Buffer Cache Impact :** The execution time for query Q afterwards was recorded to be 8.42 , as the buffer cache / shared buffer warmed up and the application was directly able to look into the `shared buffer` directly instead of the redirection.
