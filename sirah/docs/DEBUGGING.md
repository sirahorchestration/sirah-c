This could be because:

The server binary wasn't recompiled with my pod creation fix
There's a crash in the new code I added to endpoint_create_pod()
The server process isn't running at all
Let me help you debug:

First, check if the server is running:

If it's not running, start it and capture any crash output:

Then try the curl command and check the logs:

If you want to do a clean rebuild:

Can you run these commands and share:

Whether the server starts without errors
Any crash/error messages from the logs
The output of ps aux | grep sirah-apiserver