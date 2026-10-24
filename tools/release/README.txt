teta-wot-cpp {version} for Windows x86_64
=========================================

teta-wot-cpp serves laboratory hardware and software as W3C Web of Things
Things, from C++, answering on the wire as teta-wot does. This package holds
its demo: a simulated microscope (a stage, a camera with a live MJPEG preview,
and an autofocus), built for Windows x86_64. To build your own Things, use the
source (https://github.com/lucaswewa/teta-wot-cpp) and its guide.

simulated-microscope.exe
    The microscope as a Thing server:

        simulated-microscope.exe -c microscope.json

    Then open http://127.0.0.1:5000/docs for the interactive API, or
    http://127.0.0.1:5000/camera/preview/viewer for the live preview.
    Without arguments it runs a short demonstration in-process and exits.

microscope-service.exe
    The same server as a Windows service (from an administrator console):

        microscope-service.exe install --port 5000
        Start-Service teta-wot-microscope
        Stop-Service teta-wot-microscope
        microscope-service.exe uninstall

    It writes its configuration and log next to itself. Without arguments
    it demonstrates stopping a server during a long move, and exits.

demo.py                  a scripted session with LabThings' Python client
                         (pip install labthings-fastapi):
                         python demo.py --spawn simulated-microscope.exe
CHANGELOG.md             what changed in this release
LICENSE                  teta-wot-cpp's licence (MIT)
THIRD_PARTY_NOTICES.md   what the repository holds from others
THIRD_PARTY_LICENSES.txt the licences of the libraries the programs contain
