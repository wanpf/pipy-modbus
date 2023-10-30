((

  initCfg = JSON.decode(pipy.load('init.json')),

  dataDir = initCfg?.dataDir || '/tmp/data',

  parserData = json => (
    (
      pos,
      uuid,
      addr,
      objs = {},
    ) => (
      console.log('json:', json),
      Object.keys(json).forEach(
        name => (
          ((pos = name.lastIndexOf('_')) > 0) && (
            uuid = name.substring(0, pos),
            addr = name.substring(pos + 1),
            uuid && addr && (
              !objs[uuid] && (
                objs[uuid] = {},
                objs[uuid].uuid = json.id,
                objs[uuid].name = uuid,
                // objs[uuid].createdAt = new Date(new Date(json.time).valueOf() + 3600 * 8 * 1000).toISOString(),
                objs[uuid].createdAt = new Date(json.time).toISOString(),
                objs[uuid].records = {}
              ),
              objs[uuid].records[addr] = '' + json[name]
            )
          )
        )
      ),
      objs
    )
  )(),

) => pipy({
  _json: null,
  _objs: null,
  _filename: null,
})

.import({
  __deviceConfig: 'rest',
})


// query config from strapi
// .task(initCfg.queryInterval)
.task('1s')
.onStart(
  () => (
    new Data
  )
)
.use('rest.js', 'query')


/*
// collect data
.task(initCfg.collectInterval)
.onStart(
  () => (
    new Data
  )
)
.branch(
  () => __deviceConfig?.actived, (
    $=>$.use('worker.js', 'collect')
  ), (
    $=>$.replaceStreamStart(
      () => new StreamEnd
    )
  )
)
//*/


.listen(8443)
.demuxHTTP().to(
  $=>$.replaceMessage(
    msg => (
      _json = JSON.decode(msg.body),
      _objs = _json && parserData(_json),
      Object.keys(_objs || {}).forEach(
        uuid => (
          _objs[uuid].ip = __inbound.remoteAddress,
          _filename = dataDir + '/' + uuid + '_' + new Date().toISOString(),
          os.writeFile(_filename, JSON.encode({data: _objs[uuid]}))
        )
      ),
      new Message({status: 200})
    )
  )
)


// post data to strapi
// .task(initCfg.postInterval)
.task('1s')
.onStart(
  () => (
    new Data
  )
)
.branch(
  () => __deviceConfig?.actived, (
    $=>$.use('worker.js', 'post')
  ), (
    $=>$.replaceStreamStart(
      () => new StreamEnd
    )
  )
)

)()
