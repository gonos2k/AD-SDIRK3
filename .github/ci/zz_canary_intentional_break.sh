#!/usr/bin/env bash
# CANARY (branch-protection proof): intentional syntax error to fail
# fast-contracts. This branch is disposable and will be deleted.
if [ ; then
  echo unreachable
fi
