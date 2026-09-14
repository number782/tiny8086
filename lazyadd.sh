#!/bin/bash
# lazyadd.sh - Stage all changes and commit with a message
# Usage: ./lazyadd.sh "commit message"

cd "$(dirname "$0")"

if [ -z "$1" ]; then
    echo "Usage: $0 \"commit message\""
    exit 1
fi

git add -A
git commit -m "$1"