"""WEB-ARCH-1: page/large-download callbacks only enqueue captured sockets.
The background work never reads the WebServer's changing current request.
"""
from pathlib import Path
s = (Path(__file__).parent.parent / 'web_rc.ino').read_text()
for route in ['/', '/wifi', '/telemetry', '/logs.csv', '/diag/trace.csv', '/diag/retained.csv']:
    start = s.index(f'webRCServer.on("{route}", HTTP_GET')
    end = s.index('\n    });', start)
    handler = s[start:end]
    assert 'enqueueWebPage(' in handler or 'enqueueWebBulkWork(' in handler, route
    if 'enqueueWebBulkWork(' in handler:
        queued = handler[handler.index('enqueueWebBulkWork('):]
        assert 'client = webRCServer.client()' in queued, route + ': capture socket before enqueue'
        worker = queued[queued.index(']() mutable {') + len(']() mutable {'):queued.rindex('}, submitWebBulk')]
        assert 'webRCServer' not in worker, route + ': worker must not access a later request'
assert 'xQueueSend(webBulkQueue, &job, 0)' in s, 'admission cannot block control HTTP task'
assert 'job->run();' in s and 'delete job;' in s
assert 'responsiveWebRCServer.releaseClientForBulkResponse();' in s
release = s[s.index('void releaseClientForBulkResponse()'):s.index('uint32_t idleDropCount()')]
assert '_chunked = false;' in release and '_currentClient = NetworkClient();' in release
print('web bulk route ownership contracts: PASS')
