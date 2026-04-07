run 
"remote camera.exe" >
in "Common Documents" ( %PUBLIC%\dOCUMENTS) will be created public (RSA1) and private (RSA2) rsa keys.
copy RSA1 key to server.
run
"camera server.exe" *i
for install and start service ( "camera server.exe" *u - for stop and uninstall)
run 
`"remote camera.exe" *rsa2_key_name*server_ip` or simply "remote camera.exe" and enter rsa2_key_name and server_ip in ui
