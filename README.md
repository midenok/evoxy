# Evoxy: libev-based HTTP proxy

### Current status: in development
#### Works well:
* Any HTTP 1.0, 1.1 requests/responses should be passed without corruption
* Request headers will be modified according to RFC2616

#### Not well:
* Performance will be slow on Keep-Alive requests for multiple destinations
* DNS is resolved synchronously

# Building

```
$ ./prepare make
$ build/evoxy --help
$ build/evoxy -v
```

By default `evoxy` will listen on port `9000`.

# TODO

See [Issues](https://github.com/midenok/evoxy/issues)
