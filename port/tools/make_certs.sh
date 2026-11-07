#!/bin/sh
# HTTPS for the LAN server (web/serve.py --https): a phone's browser gives WebGPU and threads (SharedArrayBuffer) only
# to a secure origin, and http://<this Mac>:8642 isn't one. Makes certs/ (gitignored):
#   ca.crt      a certificate authority of our own, to install on the phone (and trust in its settings)
#   server.crt  this machine's certificate, signed by it, for its LAN addresses and names
#   server.key  its key
# The authority's key is deleted once it has signed: a trusted authority whose key exists could sign for any site,
# so this one signs nothing else, ever. A new address or an expired server certificate (397 days, the most iOS
# accepts) means running this again and installing the new ca.crt.
#
#     sh port/tools/make_certs.sh [extra address or name ...]
set -eu
cd "$(dirname "$0")/../.."
mkdir -p certs
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

host=$(scutil --get LocalHostName 2>/dev/null || hostname -s)
san="DNS:localhost,DNS:$host.local,IP:127.0.0.1"
for interface in en0 en1 en2; do
    address=$(ipconfig getifaddr "$interface" 2>/dev/null || true)
    [ -n "$address" ] && san="$san,IP:$address"
done
for extra in "$@"; do
    case "$extra" in
        *[!0-9.]*) san="$san,DNS:$extra" ;;
        *) san="$san,IP:$extra" ;;
    esac
done

openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 3650 \
    -keyout "$work/ca.key" -out certs/ca.crt \
    -subj "/CN=THUG2 Web local CA ($host, $(date +%Y-%m-%d))" \
    -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null

openssl req -newkey rsa:2048 -nodes -sha256 -keyout certs/server.key -out "$work/server.csr" \
    -subj "/CN=$host.local" 2>/dev/null
cat > "$work/server.ext" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=$san
EOF
openssl x509 -req -sha256 -days 397 -in "$work/server.csr" -CA certs/ca.crt -CAkey "$work/ca.key" \
    -set_serial "0x$(openssl rand -hex 8)" -extfile "$work/server.ext" -out certs/server.crt 2>/dev/null
chmod 600 certs/server.key

echo "certs/ca.crt (install on the phone), certs/server.crt for: $san"
