#!/bin/bash
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <path_to_proto_file> <output_directory>"
fi

SOURCE_FILE=${1}
SOURCE_DIR=$(dirname $( dirname "${SOURCE_FILE}" ))
GENERATED_DIR=${2}

if [ ! -f "env/bin/activate" ]
    then
        python3.8 -m venv env
fi

source env/bin/activate
pip install -q -U "grpcio-tools==1.44.0"

mkdir -p "${GENERATED_DIR}"

echo " -- Building ${SOURCE_FILE}"
python -m grpc_tools.protoc \
        -I "${SOURCE_DIR}" \
        --python_out "${GENERATED_DIR}" \
        --grpc_python_out "${GENERATED_DIR}" \
        "${SOURCE_FILE}"