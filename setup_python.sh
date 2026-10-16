#!/usr/bin/env bash
# Source this file in Bash to use the locally built SDK with system Python.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    printf 'Use: source %s\n' "$0" >&2
    exit 1
fi
_rphand_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)" || return 1
if [[ ! -f "$_rphand_root/install/python/rphand/__init__.py" ]]; then
    printf 'SDK not built. Run: bash "%s/build.sh" python\n' "$_rphand_root" >&2
    unset _rphand_root
    return 1
fi
case ":${PYTHONPATH:-}:" in
    *":$_rphand_root/install/python:"*) ;;
    *) export PYTHONPATH="$_rphand_root/install/python${PYTHONPATH:+:$PYTHONPATH}" ;;
esac
unset _rphand_root
# The default build embeds the SDK core in the Python extension.
