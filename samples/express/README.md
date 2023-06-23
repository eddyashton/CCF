# CCF Express

## Summary
This folder contains a small tool to host a CCF JS app using ExpressJS. This uses the polyfills distributed with the `ccf-app` npm package to implement the `ccf` global, and exposes HTTP routes based on the metadata from an `app.json` file.

The intention is that this can be used to debug CCF application handlers.

### *WARNINGS*
This is a minimal PoC, and does not provide a perfectly matching server implementation. Notable discrepancies from a CCF node at the time of writing:
- No implementation of governance endpoints. This purely serves `/app` endpoints, and any attempts at governance will return 404s. Any app state which is bootstrapped by governance may need additional genesis calls.
- Does no authentication. It attempts to construct a valid `caller` object based on fields from the incoming request, but does not validate that this represents an identity that is known and trusted in the KV.
- KV does not offer opacity. Any writes to the KV are immediately visible to other concurrently executing handlers, and writes are rolled back for failing operations.
- Error response body shape is inconsistent. This does not produce JSON OData responses for errors in the same way a CCF node does.

In short, this should only be used for testing happy-path execution flows. Any flows which rely on error responses or framework-inserted details are not supported out-of-the-box, and will require extensions to this tool.

## Use

## Implementation

