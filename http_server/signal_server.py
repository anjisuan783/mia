#-*- coding:utf-8 –*-

import asyncio
import websockets
import websockets_routes
import json
import ssl
import signal
import pdb

SERVICE_IP = '0.0.0.0'
SERVICE_PORT = 8443

class Peer():
  def __init__(self, uid, ws):
    self.uid = uid
    self.ws = ws

  async def SendMessage(self, msg):
    await self.ws.send(msg)

  async def Close(self):
    await self.ws.close()

  @property
  def Uid(self):
    return self.uid

class PeerManager():
  def __init__(self):
    self.peers = {}

  def OnMessage(self, uid, msg):
    pass

  def AddPeer(self, uid, pc):
    self.peers[uid] = pc

  def RemovePeer(self, uid):
    self.peers.pop(uid)

  async def SendTo(self, to_uid, msg):
    pc = self.peers.get(to_uid)

    if not pc:
      print(f'peer:{to_uid} not found')
      return

    await pc.SendMessage(msg)

  def dump(self):
    str_uid_monitor = f'online total:{len(self.peers)} users:'
    for pc in self.peers:
      str_uid_monitor += '[{}]'.format(pc)
    
    print(f'{str_uid_monitor}')

manager = PeerManager()
router = websockets_routes.Router()

signal_quit = False

@router.route("/websocket/{uid}")
async def OnPeer(websocket, path):
  uid = int(path.params['uid'])
  print(f'peer join {uid}')   
  pc = Peer(uid, websocket)
  manager.AddPeer(uid, pc)
  try:
    async for message in websocket:
      print(f'cmd {message}')
      try:
        cmd = json.loads(message)
      except Exception as e:
        print(f"decode msg from uid:{uid} failed, desc:{e}")
        raise websockets.exceptions.InvalidMessage
      else:
        self_uid = cmd['userId']
        assert(self_uid == uid)
        toUid = cmd['toUserId']
        #cmd['message']['type']
        await manager.SendTo(toUid, message)
  except Exception as e:
    print('recv Exception {}'.format(e))
  finally:
    print(f'Remote close uid:{uid}')
    manager.RemovePeer(uid)
    await pc.Close()

async def main():
  ssl_context = ssl.SSLContext()
  ssl_context.load_cert_chain('../conf/server.cer', '../conf/server.key')
  async with websockets.serve(
      lambda x, y: router(x, y), SERVICE_IP, SERVICE_PORT, ssl=ssl_context):
    print("signal server start")

    while not signal_quit:
      manager.dump()
      await asyncio.sleep(2)

  print("signal server stop")

def signal_handler(signum, frame):
  global signal_quit
  signal_quit = True

if __name__ == "__main__":
  signal.signal(signal.SIGINT, signal_handler)
  asyncio.run(main())
