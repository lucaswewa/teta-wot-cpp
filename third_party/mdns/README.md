# mjansson/mdns

[mjansson/mdns](https://github.com/mjansson/mdns) 1.4.3, a public domain (Unlicense) mDNS/DNS-SD library in C: `mdns.h` and its `LICENSE`, unchanged. Taken from the release tarball (`https://github.com/mjansson/mdns/archive/1.4.3.tar.gz`), whose SHA-512 is the one vcpkg's `mdns` port pins:

```
0bbfeefdd3f324a8e5aa85227bfa45c2b5cd88c12a9f77df2a1c48cb2661ba8b283dd53541e39d20ed2705646dc8d8724a0287c58f9efa91d2b1b796a0ca9a7a
```

teta-wot-cpp's DNS-SD advertiser (`src/server/mdns.cpp`, with the CMake option `TETA_WOT_MDNS`) and the `discovery` example use it.
