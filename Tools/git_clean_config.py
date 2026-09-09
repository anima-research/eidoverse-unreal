"""Keep the Android File Server's local development token out of Git history."""
import re
import sys
sys.stdout.write(re.sub(r'(?m)^SecurityToken=.*$', 'SecurityToken=', sys.stdin.read()))
