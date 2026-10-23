# Docs assets

Swagger UI 5.33.0 and ReDoc 2.5.4, as teta-wot v0.1.0 vendors them (`crates/teta-wot-http/assets/`), with their licences. The `/docs` and `/redoc` pages load them from jsDelivr; built with `TETA_WOT_DOCS_OFFLINE=ON`, teta-wot-cpp compiles them in and serves them itself. `oauth2-redirect.js` is always compiled in, inline in `/docs/oauth2-redirect`.

They are stored byte for byte (`.gitattributes`: `-text`), so every checkout serves the same bytes as teta-wot.
