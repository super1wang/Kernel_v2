import json
import pathlib
import subprocess
import sys
import uuid

service,cli=map(lambda value:str(pathlib.Path(value).resolve()),sys.argv[1:3])
work=pathlib.Path(sys.argv[3])/uuid.uuid4().hex
work.mkdir(parents=True)
source=work/"中文输入.json"
source.write_text(json.dumps({"amount":4,"text":"中文 café 🚀"},ensure_ascii=False),encoding="utf-8")
instance="shell-"+uuid.uuid4().hex
server=subprocess.Popen([service,"--instance",instance],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8")
def ps(value):return "'"+str(value).replace("'","''")+"'"
try:
    assert json.loads(server.stdout.readline())=={"ready":True}
    for shell in ["cmd-file","cmd-stdin","powershell-file","powershell-stdin"]:
        output=work/(shell+".json")
        if shell.startswith("cmd"):
            script=work/(shell+".cmd")
            invocation=f'"{cli}" --instance {instance} --json invoke sample.increment '
            command=invocation+f'--file "{source}"' if shell.endswith("file") else f'type "{source}" | '+invocation+'--stdin'
            script.write_text('@echo off\nchcp 65001 >nul\n'+command+f' > "{output}"\nexit /b %errorlevel%\n',encoding="utf-8",newline="\r\n")
            argv=["cmd.exe","/d","/c",str(script)]
        else:
            script=work/(shell+".ps1")
            invocation=f'& {ps(cli)} --instance {instance} --json invoke sample.increment '
            command=invocation+f'--file {ps(source)}' if shell.endswith("file") else f'Get-Content -LiteralPath {ps(source)} -Raw -Encoding UTF8 | '+invocation+'--stdin'
            script.write_text('$OutputEncoding = [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)\n'+
                '$result = '+command+'\n$code = $LASTEXITCODE\n'+
                f'[System.IO.File]::WriteAllText({ps(output)}, ($result -join "`n"), (New-Object System.Text.UTF8Encoding($false)))\nexit $code\n',encoding="utf-8-sig")
            argv=["powershell.exe","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",str(script)]
        result=subprocess.run(argv,capture_output=True,timeout=25)
        (work/(shell+".stdout.bin")).write_bytes(result.stdout)
        (work/(shell+".stderr.bin")).write_bytes(result.stderr)
        assert result.returncode==0,(shell,result.returncode,result.stdout,result.stderr)
        reply=json.loads(output.read_text(encoding="utf-8-sig"))
        assert reply["result"]["outcome"]["result"]=={"amount":5,"text":"中文 café 🚀"},reply
        print(shell,"UTF-8 and one machine result passed")
    print("raw_shell_artifacts",work.resolve())
finally:
    if server.poll() is None:
        try:server.communicate("stop\n",timeout=10)
        except subprocess.TimeoutExpired:server.kill();server.communicate()
    assert server.returncode==0,server.returncode
