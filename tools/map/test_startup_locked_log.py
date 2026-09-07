"""Windows regression: engine startup must survive a log that cannot be rotated.

Runs the actual development executable, holding log.txt open without delete sharing.
Use --keep to leave the successfully started editor available on --port (47786).
"""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import socket
import subprocess
import time

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--port',type=int,default=47786);parser.add_argument('--keep',action='store_true')
 args=parser.parse_args();root=Path(__file__).resolve().parents[2];run=root/'binaries'
 with socket.socket() as probe:
  assert probe.connect_ex(('127.0.0.1',args.port))!=0,'Test port already in use'
 kernel=ctypes.WinDLL('kernel32',use_last_error=True)
 kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
 kernel.CreateFileW.restype=wintypes.HANDLE
 kernel.CloseHandle.argtypes=[wintypes.HANDLE]
 handle=kernel.CreateFileW(str(run/'log.txt'),0x80000000,3,None,4,0x80,None)
 assert handle!=wintypes.HANDLE(-1).value,ctypes.get_last_error()
 process=None;passed=False
 try:
  # Prove the fixture triggers the Windows sharing violation; never remove the log.
  kernel.CreateFileW.restype=wintypes.HANDLE
  deletion=kernel.CreateFileW(str(run/'log.txt'),0x00010000,7,None,3,0x80,None)
  if deletion!=wintypes.HANDLE(-1).value:
   kernel.CloseHandle(deletion);raise AssertionError('Fixture did not block deletion')
  assert ctypes.get_last_error()==32,'Expected ERROR_SHARING_VIOLATION'
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  process=subprocess.Popen([str(run/'spartan_vulkan_development.exe'),'--mcp-control',f'--mcp-port={args.port}'],cwd=run,startupinfo=startup)
  deadline=time.monotonic()+45
  while time.monotonic()<deadline:
   assert process.poll() is None,f'Engine crashed during startup: {process.returncode}'
   try:
    with socket.create_connection(('127.0.0.1',args.port),timeout=2) as conn:
     conn.settimeout(5);conn.sendall(b'context_snapshot\n');result=json.loads(conn.makefile('rb').readline())
     if result.get('ok') and result.get('status',{}).get('time_seconds',0)>2:
      passed=True;print(json.dumps({'passed':True,'pid':process.pid,'locked_log':True,'status':result['status']}),flush=True);break
   except (OSError,ValueError):pass
   time.sleep(.5)
  assert passed,'Engine did not respond within 45 seconds'
 finally:
  kernel.CloseHandle(handle)
  if process is not None and not (passed and args.keep):
   process.terminate();process.wait(timeout=10)

if __name__=='__main__':main()
