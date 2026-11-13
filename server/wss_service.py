import importlib.util,json,os,pathlib,socket,sys,threading
base=pathlib.Path('/workspaces/.csos-smoke')
config=json.loads((base/'bridge-private.json').read_text())
os.environ['DISPLAY']=':77'
os.environ['XAUTHORITY']=str(base/'Xauthority')
if __name__ == '__mp_main__':
    pass
elif sys.argv[1]=='bridge':
    import multiprocessing
    multiprocessing.set_start_method('fork', force=True)
    from websockify import WebSocketProxy
    from websockify.auth_plugins import BasicHTTPAuth
    WebSocketProxy(listen_host='127.0.0.1',listen_port=8080,target_host='127.0.0.1',target_port=5901,
        auth_plugin=BasicHTTPAuth(config['username']+':'+config['password']), heartbeat=15).start_server()
else:
    spec=importlib.util.spec_from_file_location('cxl_original',base/'cxl_remote_server.py')
    cxl=importlib.util.module_from_spec(spec);spec.loader.exec_module(cxl)
    class Server:
        inp=cxl.X11Input();monitor=0;view_only=False;verbose=False;matrix=None
        fps_probe=(base/'fps-probe.enabled').is_file()
        adaptive_rectangles=(base/'adaptive-rectangles.enabled').is_file()
        cfg={'pin':config['cxl_pin'],'require_pin':True,'fps':5,'stretch':True,'audio':False,'cursor':True}
        def remember(self,**kw):pass
    from csos_touch import absolute_session
    TouchSession=absolute_session(cxl)
    from csos_presence import with_presence
    TouchSession=with_presence(TouchSession)
    from csos_quality import with_quality_refinement
    TouchSession=with_quality_refinement(TouchSession,cxl.TILE_W,cxl.TILE_H,
        base=cxl if (base/'regional-video.enabled').is_file() else None)
    # Qualified path is the default, including a fresh Codespace. Explicit opt-out
    # remains available without depending on a marker left by a hardware trial.
    if not (base/'lossless-video.disabled').is_file():
        from csos_video import with_lossless_video
        TouchSession=with_lossless_video(TouchSession,cxl.tiles_to_rects,cxl.TILE_W,cxl.TILE_H)
    from csos_audio import with_audio
    TouchSession=with_audio(TouchSession)
    server=Server()
    with socket.create_server(('127.0.0.1',5901)) as listener:
        print('CXL loopback ready',flush=True)
        while True:
            connection,address=listener.accept()
            threading.Thread(target=TouchSession(connection,address,server).run,daemon=True).start()
