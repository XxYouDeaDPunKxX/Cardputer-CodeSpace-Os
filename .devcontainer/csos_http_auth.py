"""Keep original Basic verification; explicitly frame every auth refusal."""
import binascii
from websockify.auth_plugins import AuthenticationError, BasicHTTPAuth


class FramedBasicHTTPAuth(BasicHTTPAuth):
    def authenticate(self, headers, target_host, target_port):
        try:
            return super().authenticate(headers, target_host, target_port)
        except binascii.Error:
            raise AuthenticationError(response_code=403,
                                      response_headers={'Content-Length': '0'}) from None
        except AuthenticationError as exc:
            exc.headers = dict(exc.headers)
            exc.headers['Content-Length'] = '0'
            raise
