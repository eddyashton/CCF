Exploring whether we can implement a generic "offload requests to an external trusted executor" application as a CCF app. Initially entirely HTTP + JSON, will convert that to gRPC later/as needed.

#### Demo

Start a sandbox service:
```
../tests/sandbox/sandbox.sh -p libgrpc_dispatcher
```

Simulate a governance process saying "here's a code ID we trust to execute this type of request":
```
$ curl -k https://127.0.0.1:8000/app/executors/code_ids --data-binary '{"code_id": "abcd", "supported_operations": [{"uri_path": "/foo", "verb": "GET"}]}' -H "Content-type: application/json"
```

Simulate an executor approaching a node, presenting an attestation proving the above code ID, and registering its public identity:
```
$ cat ./register.json
{
  "attestation": {
    "code_id": "abcd"
  },
  "identity": "-----BEGIN CERTIFICATE-----\nMIIBszCCATigAwIBAgIUJmDQxxV/SDwrYLTnjIXYBfR4uMYwCgYIKoZIzj0EAwMw\nEDEOMAwGA1UEAwwFdXNlcjAwHhcNMjIwODAzMTMxOTUxWhcNMjMwODAzMTMxOTUx\nWjAQMQ4wDAYDVQQDDAV1c2VyMDB2MBAGByqGSM49AgEGBSuBBAAiA2IABFFO3Xp6\nZHSgU2OjL0mSD97kBp58GfZStBBXsIOF6F7mdqVFDYtgmlwknIqDQLuuRrqlbfT4\n/YKHir1AsoHTXzVjKF10e8aSJAT5PxU8/6wL6LaBCzHDXr0mmx8pf9iGtqNTMFEw\nHQYDVR0OBBYEFDmOFGthBpyp2QC349S5dotJytMeMB8GA1UdIwQYMBaAFDmOFGth\nBpyp2QC349S5dotJytMeMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwMDaQAw\nZgIxALPOxTXiLWOiE1jUYU6W2DHBTBzT0B1jrHzNalV8776/RY55moLWlremU8/J\nNbVvmwIxAI1iRufgskbQsM2+yb1gBfncxUezhzAeFRvSAsTk1SxlqFD3OiofW++A\nd4qEBUPWhg==\n-----END CERTIFICATE-----\n"

$ curl -k https://127.0.0.1:8000/app/register -H "Content-Type: application/json" --data-binary @register.json
```

Try calling that endpoint:
```
$ curl -k https://127.0.0.1:8000/app/foo
{"error":{"code":"InternalError","message":"Exception: basic_string"}}
```

Note that looks like an error because we didn't actually do anything yet, but the node log shows more of the working flow:
```
2022-08-03T13:54:05.717030Z -0.001 0   [info ][app] cher/app/grpc_dispatcher.cpp:44 | Inserting code_id: 69 b7 1d

2022-08-03T13:54:10.422307Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:223 | Registering new executor: {"attestation":{"code_id":"abcd"},"identity":"-----BEGIN CERTIFICATE-----AAA-----END CERTIFICATE-----\n"}
2022-08-03T13:54:10.422348Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:264 | Successfully registered
2022-08-03T13:54:10.422353Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:132 | Rebuilding dispatch table
2022-08-03T13:54:10.422365Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:151 | Rebuilding dispatch table at 13
2022-08-03T13:54:10.422369Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:180 | Resulting dispatch table contains 1 entries
2022-08-03T13:54:10.422372Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:183 |   'GET /foo' can go to 1 executors
2022-08-03T13:54:10.422376Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:186 |     -----BEGIN CERTIFICATE-----AAA----END CERTIFICATE-----

2022-08-03T13:54:15.509930Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:132 | Rebuilding dispatch table
2022-08-03T13:54:15.509970Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:193 | Dispatch table is still valid
2022-08-03T13:54:15.509976Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:314 | Have 1 possible executors
2022-08-03T13:54:15.509981Z -0.001 0   [info ][app] her/app/grpc_dispatcher.cpp:324 | Chose to execute on -----BEGIN CERTIFICATE-----AAA-----END CERTIFICATE-----
2022-08-03T13:54:15.509986Z -0.001 0   [fail ] ../src/http/http_endpoint.h:260      | Closing connection
```

### Some Notes

- This selects supported URLs per trusted code ID, not per-executor
- The caller's identity is submitted explicitly, in a request body, because we think this makes it simpler to offload this step and then hand-over the identity from a sidecar. That's a little clunky, and it might be more natural to extract the calling executor's identity from their TLS context, perhaps as a separate /register/self endpoint
- The remaining big questions for this approach are how we extend/redefine the lifetime of a Tx, and how we forward this request to the executor
- The cached dispatcher table is extremely sketchy, and needs a close reading
