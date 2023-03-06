((
  { config } = pipy.solve('config.js'),

  records = ((str = '') => (
    (config?.records || []).forEach(
      e => (
        str += e.fc + ' ' + e.addr + ' ' + e.type + '\n'
      )
    ),
    str
  ))(),

  dataDir = config?.dataDir || '/tmp/data',
  ignoreOnce = true,
  jsonLogging = config?.loggerUrl && new logging.JSONLogger('json-logger').toHTTP(config.loggerUrl,
  {
    batch: {
      timeout: 1,
      interval: 1,
      prefix: '[',
      postfix: ']',
      separator: ','
    },
    headers: {
      'Content-Type': 'application/json',
      'Authorization': config.loggerToken,
    }
  }).log,
) => (

pipy({
  _filename: undefined,
})

.import({
  __modbusDeviceName: 'modbus-nmi',
  __modbusSlaveID: 'modbus-nmi',
  __modbusBaud: 'modbus-nmi',
  __modbusRecords: 'modbus-nmi',
})

.task()
.onStart(
  () => (
    os.readDir(dataDir).sort().filter(s => !s.endsWith('/')).map(
      s => (
        console.log('[=== reload ===]', dataDir + '/' + s),
        new Message(dataDir + '/' + s)
      )
    )
  )
)
.demuxQueue().to('queue')

// 定时器，每3秒采集一次 modbus 数据
.task('3s')
.onStart(
  () => (
    __modbusDeviceName = config?.deviceName || '/dev/ttyUSB0',
    __modbusSlaveID = config?.slaveID || 1,  
    __modbusBaud = config?.band || 9600,
    __modbusRecords = records || '',
    new Message()
  )
)
.branch(
  () => ignoreOnce, ( // pipy 启动时，忽略第一次任务
    $=>$.replaceMessageEnd(
      () => (
        ignoreOnce = false,
        new StreamEnd
      )
    )
  ), (
    $=>$
    .use('./modbus-nmi.so')
    // .dump('=== modbus-nmi ===')
    .replaceMessage(
      msg => (
        msg?.body ? (
          _filename = dataDir + '/' + new Date().toISOString(),
          os.writeFile(_filename, msg?.body)
        ) : (
          _filename = undefined,
          console.log("modbus-nmi error: ", msg?.head?.error)
        ),
        new Message()
      )
    )
    .branch(
      () => !Boolean(_filename), (
        $=>$.replaceMessageEnd(
          () => new StreamEnd // modbus 没有数据返回，无需发送消息
        )
      ),
      () => Boolean(_filename), (
        $=>$
        .replaceMessage(
          () => [new Message(_filename), new StreamEnd]
        )
        .link('queue')
      ),
    )
  )
)

.pipeline('queue')
.replaceMessage(
  msg => (
    _filename = msg.body.toString(),
    new Message({method: 'POST', headers: {'Content-Type': 'application/json'}}, os.readFile(_filename))
  )
)
.replay({delay: 5}).to(
  $=>$
  .muxHTTP(() => '').to(
    $=>$.connect('127.0.0.1:8088') 
  )
  .replaceMessage(
    msg => (
      msg?.head?.status === 200 ? (
        os.unlink(_filename),
        // console.log('os.unlink(_filename) : ', _filename),
        new StreamEnd
      ) : new StreamEnd('Replay')
    )
  )
)

.listen('127.0.0.1:8088')
.demuxHTTP().to(
  $=>$.replaceMessage(
    msg => (
      msg?.body?.size > 0 && jsonLogging?.({message: msg.body.toString()}),
      console.log("[=== REST server received JSON message ===]", msg?.body),
      new Message('OK')
    )
  )
)

))()
