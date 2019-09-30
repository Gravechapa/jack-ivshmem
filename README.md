# scream-ivshmem-jack

scream-ivshmem-jack is a scream receiver using JACK Audio Connection Kit as audio output and IVSHMEM to share the ring buffer between guest and host.

## Compile

You need JACK Audio Connection Kit headers in advance.

```shell
$ sudo yum install libjack-devel # Redhat, CentOS, etc.
or
$ sudo apt-get install libjack-dev # Debian, Ubuntu, etc.
```

Run `make` command.

## Usage

Launch your VM, make sure to have read permission for the shared memory device and execute

```shell
$ scream-ivshmem-jack /dev/shm/scream-ivshmem
```
## TODO

Implement resampling
