import base64
import hashlib
import os


class FilterModule:
    def filters(self):
        return {"mosquitto_pbkdf2": self.mosquitto_pbkdf2}

    @staticmethod
    def mosquitto_pbkdf2(password):
        salt = os.urandom(12)
        digest = hashlib.pbkdf2_hmac("sha512", str(password).encode(), salt, 101)
        return "$7$101${}${}".format(
            base64.b64encode(salt).decode(), base64.b64encode(digest).decode()
        )
