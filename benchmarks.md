# NXWEB 3.0 Benchmarks

Results are in thousands requests per second. F = failed. NA = not supported. Measured by [httpress](https://github.com/yarosla/httpress) on 4-core CPU (without AES-NI).

|Test              | NXWEB    | G-WAN  | libevent  | microhttpd | mongoose | nginx |
|------------------|-----------|-------|-----------|-----|-----|-----|
|1. hello 100 ka   | 200 / 121 | 144   | 30 / 69   | 132 | 190 | 141 |
|2. hello 100      | 51 / 42  | 41     | 15 / 32   | 13  | 34  | 41  |
|3. hello 1000 ka  | 160 / 115 | 130   | 21 / 43   | 130 | 180 | 124 |
|4. hello 1000     | 46 / 38  | 38     | 14 / 30   | 12  | 35  | 40  |
|5. hello 10000 ka | 115 / 84 | 103    | 23 / 40   | 116 | 119 | 108 |
|5.1. real concurrency| 9500-10000 | 9500-10000 | 10000 | 600-1000 | 1500-1700 | 4000-7000 |
|5.2. memory footprint| 28Mb  | 105Mb  | -         | -   | -   | 4x15Mb |
|6. hello 10000    | 38 / 34  | 33     | 14 / 27   | 9   | 20  | 29  |
|7. file 2.3K ka   | 133      | NA     | NA        | NA  | 5   | 98  |
|7.1. file 2.3K ka cached | 145 | 120  | NA        | NA  | NA  | NA  |
|8. file 2.3K      | 42       | NA     | NA        | NA  | 12  | 39  |
|8.1. file 2.3K cached    | 43  | 33   | NA        | NA  | NA  | NA  |
|9. file 100K ka   | 36       | 15     | NA        | NA  | 3.6 | 32  |
|10. file 100K     | 23       | 12     | NA        | NA  | 3.5 | 21  |
|11. file 2.1M ka  | 3.2      | 0.7    | NA        | NA  | 0.6 | 2.6 |
|12. file 2.1M     | 2.3      | 0.6    | NA        | NA  | 0.5 | 2.0 |

### Test descriptions:

1. Minimal handler returning `<p>Hello, world!</p>`; 100 concurrent, keep-alive (httpress -c 100 -n 1000000 -t 4 -k)
1. Minimal handler returning `<p>Hello, world!</p>`; 100 concurrent, no keep-alive (httpress -c 100 -n 500000 -t 4)
1. Minimal handler returning `<p>Hello, world!</p>`; 1000 concurrent, keep-alive (httpress -c 1000 -n 1000000 -t 4 -k)
1. Minimal handler returning `<p>Hello, world!</p>`; 1000 concurrent, no keep-alive (httpress -c 1000 -n 500000 -t 4)
1. Minimal handler returning `<p>Hello, world!</p>`; 10000 concurrent, keep-alive (httpress -c 10000 -n 1000000 -t 4 -k)
    1. httpress tool allows to calculate real concurrency, the number of actually active connections participating in test
    1. 10K concurrent connections take a lot of RAM. Here you can see how much
1. Minimal handler returning `<p>Hello, world!</p>`; 10000 concurrent, no keep-alive (httpress -c 10000 -n 500000 -t 4)
1. Disk file 2.3 KiB; 400 concurrent connections, keep-alive
    1. Disk file 2.3 KiB; 400 concurrent connections, keep-alive, using memory cache
1. Disk file 2.3 KiB; 400 concurrent connections, no keep-alive
    1. Disk file 2.3 KiB; 400 concurrent connections, no keep-alive, using memory cache
1. Disk file 100 KiB; 400 concurrent connections, keep-alive
1. Disk file 100 KiB; 400 concurrent connections, no keep-alive
1. Disk file 2.1 MiB; 400 concurrent connections, keep-alive
1. Disk file 2.1 MiB; 400 concurrent connections, no keep-alive

## SSL Benchmarks

SSL INFO: ECDHE_RSA_AES_256_CBC_SHA1
- Protocol: TLS1.0
- Key Exchange: ECDHE-RSA
- Ephemeral ECDH using curve SECP256R1
- Cipher: AES-256-CBC
- MAC: SHA1
- Compression: NULL
- Certificate Type: X.509
- Certificate Info: RSA key 2048 bits, signed using RSA-SHA1

All tests done with 100 concurrent connections. Numbers are requests per second.

Eg.

    httpress -c100 -n4000 -t4 -z 'NORMAL:-CIPHER-ALL:+AES-256-CBC:-VERS-TLS-ALL:+VERS-TLS1.0'

| Test             | nxweb   | nginx  |
|------------------|---------|--------|
|1. file 2.3K ka   |  26000  | 23000  |
|2. file 2.3K      |  490    | 430    |
|3. file 100K ka   |  1300   | 1100   |
|4. file 100K      |  360    | 330    |

## Server notes:

* NXWEB: first measurement is for inprocess handler, second is for inworker handler
* [G-WAN](http://gwan.ch/): v.3.1.24 x64. Performance of this server is good, it is also very convenient to use - it autocompiles on the fly all source files put in special directory; I found it quite unstable though: segfaults on some static files, hangs on its own sample scripts; another big disadvantage is that it is closed-source
* [libevent](http://libevent.org/): standard implementation of evhttp is single-threaded; second measurement in table is received from custom multi-threaded version (several instances of evhttp launched in 4 threads, listening to the same socket); not sure if this hack affects stability
* [GNU microhttpd](http://www.gnu.org/s/libmicrohttpd/): MHD_USE_SELECT_INTERNALLY | MHD_USE_POLL, 4 thread-pool
* [mongoose](http://code.google.com/p/mongoose/): configured with 2500 threads in pool; uses thread per connection model, which is certainly not very efficient and takes a lot of memory
* [nginx](http://nginx.org): v.1.1.12 compiled from source; using optimized setup; sendfile turned on, aio/directio turned off; location /hello { return 200 `<p>Hello, world!</p>`; }
