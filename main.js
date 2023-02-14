((
  dataDir = '/tmp/data', // 设定存放数据的目录
  ignoreOnce = true,
) => (

pipy({
  _filename: undefined,
})

.import({
  __modbusDeviceName: 'modbus-nmi',
  __modbusSlaveID: 'modbus-nmi',
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
// 根据 __modbusDeviceName, __modbusSlaveID 这2个参数进行采集
.task('3s')
.onStart(
  () => (   
    __modbusDeviceName = '/dev/ttyUSB0', // 需要根据实际情况修改
    __modbusSlaveID = 17,                // 需要根据时间情况修改
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
    $=>$.connect('127.0.0.1:8088') // 接收消息的 REST 服务器，需要根据实际情况修改
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

// 测试用，模拟数据接收服务端, 生产环境请去掉如下代码：
.listen('127.0.0.1:8088')
.demuxHTTP().to(
  $=>$.replaceMessage(
    msg => (
      console.log("[=== REST server received JSON message ===]", msg?.body),
      new Message('OK')
    )
  )
)

))()