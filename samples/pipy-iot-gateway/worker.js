((

  initCfg = JSON.decode(pipy.load('init.json')),

  dataDir = initCfg?.dataDir || '/tmp/data',

  records = rec => (
    (str = '') => (
      (rec || []).forEach(e => (str += e.fc + ' ' + e.addr + ' ' + e.type + '\n')),
      str
    )
  )(),

  triggerResolve = (resolves, all) => (
    all ? (
      resolves.forEach(
        r => r()
      )
    ) : (
      resolves.pop()?.()
    )
  ),

) => (

pipy({
  _filename: undefined,
  _dataJson: undefined,
  _sendTask: 0,
  _promises: null,
  _resolves: null,
})

.import({
  __deviceConfig: 'rest',
  __modbusDeviceName: 'modbus-nmi',
  __modbusSlaveID: 'modbus-nmi',
  __modbusBaud: 'modbus-nmi',
  __modbusRecords: 'modbus-nmi',
})


// collect data by modbus-nmi
.pipeline('collect')
.onStart(
  () => (
    __modbusDeviceName = __deviceConfig?.config?.deviceName || '/dev/ttyUSB0',
    __modbusSlaveID = __deviceConfig?.config?.slaveID || 1,
    __modbusBaud = __deviceConfig?.config?.baud || 9600,
    __modbusRecords = records(__deviceConfig?.config?.records),
    _dataJson = null,
    new Message()
  )
)
.use('./modbus-nmi.so')
.replaceMessage(
  msg => (
    msg?.body && (
      _dataJson = JSON.decode(msg?.body),
      _dataJson?.ts > 0 && (
        delete _dataJson.id,
        _dataJson.created_at = new Date(_dataJson.ts * 1000).toISOString(),
        delete _dataJson.ts,
        _dataJson.uuid = __deviceConfig.uuid,
        _dataJson.name = __deviceConfig.name,
        _dataJson.device = __deviceConfig.device,
        _dataJson.drive = __deviceConfig.drive,
        _filename = dataDir + '/' + new Date().toISOString(),
        os.writeFile(_filename, JSON.encode({data: _dataJson}))
      )
    ),
    new StreamEnd
  )
)


// send to strapi
.pipeline('send')
.replaceMessage(
  msg => (
    _filename = msg.body.toString(),
    new Message(os.readFile(_filename))
  )
)
.use('rest.js', 'post')
.replaceMessage(
  msg => (
    msg?.body?.toString() === 'OK' && (
      os.unlink(_filename)
      // , console.log('Post data, filename:', _filename)
    ),
    msg
  )
)

// batch post data
.pipeline('post')
.onStart(
  () => (
    _promises = [],
    _resolves = [],
    (
      files = os.readDir(dataDir).sort().filter(s => !s.endsWith('/')), // TODO, many files?
      parts = files.slice(files.length > 10 ? files.length - 10 : 0),
    ) => (
      _sendTask = parts.length,
      parts.map(s => (
        // console.log('[=== load data ===]', dataDir + '/' + s),
        _promises.push(
          new Promise(
            r => _resolves.push(r)
          )
        ),
        new Message(dataDir + '/' + s)
      ))
    )
  )()
)
.branch(
  () => _sendTask > 0, (
    $=>$
    .demuxQueue().to(
      $=>$.link('send')
    )
    .handleMessage(
      (msg) => (
        (msg?.body?.toString?.() !== 'OK') ? (triggerResolve(_resolves, true)) : triggerResolve(_resolves)
      )
    )
    .handleStreamEnd(
      () => (
        triggerResolve(_resolves, true)
      )
    )
    .wait(
      () => Promise.all(_promises)
    )
  ), (
    $=>$
  )
)
.replaceData(
  () => new StreamEnd
)

))()

