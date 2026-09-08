"""借用 owner 释放后的真实 ASan 反例；启动失败/普通非零不冒充检测成功。"""
import json
from pathlib import Path
import subprocess
import sys
import uuid
out=Path('view-escape-'+uuid.uuid4().hex)
out.mkdir()
r=subprocess.run([sys.argv[1],'escape'],capture_output=True,timeout=20)
(out/'stdout.log').write_bytes(r.stdout)
(out/'stderr.log').write_bytes(r.stderr)
(out/'result.json').write_text(json.dumps({'argv':[sys.argv[1],'escape'],'exit_code':r.returncode}),encoding='utf-8')
print(out, 'actual exit',r.returncode)
sys.stdout.buffer.write(r.stdout);sys.stderr.buffer.write(r.stderr)
sys.exit(0 if r.returncode!=0 and b'AddressSanitizer: heap-use-after-free' in r.stdout+r.stderr else 1)
