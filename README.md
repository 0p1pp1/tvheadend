ISDB向けTvheadend
========================================

追加・修正機能
------------
- ISDB-S/Tのサポート(チューニング, EPG, 文字コード変換)
- EPG連動録画の際に、延長・中断や別chへのリレーに対応 (テスト不十分)
- transcodingの不具合修正(AAC音声が(copyであっても)壊れる, ffmpegから大量のエラーが出る)
- デスクランブル機能(外部ライブラリ使用)


ビルド方法
---------

オリジナル版のtvheadendの依存関係に加えて、`libunistring`と`libyakisoba`が必要。
libyakisobaは用意/自作する。
必要なAPIは`bcas_decodeECM()`のみ。[ソース参照](./src/descrambler/bcas.c#L33)

        $ git clone --depth 1 git://github.com/0p1pp1/tvheadend
        $ cd tvheadend
        $ ./configure --python=python2 --enable-libsystemd-daemon
        $ make
        [$ sudo make install]

実行
---------

EPG等で使用される文字コードを変換するため、事前に[iconvの変換モジュール]
(https://github.com/0p1pp1/gconv-module-aribb24.git)をインストールしておくことが必要。

        $ tvheadend --nosatip --nobat

`make install`をしていないなら、`./build.linux/tvheadend --nosatip --nobat`

(使用するDVBアダプタを限定したいなら、`--adapters 4`等を使用。 `--help`を参照)


設定上の注意
----------

基本的には[オジリナル版での設定](http://docs.tvheadend.org/configure_tvheadend/)で、
http://127.0.0.1:9981/にアクセスして設定する。
幾つか解りにくい/気付きにくい設定項目があるので注意。

- `-C`オプションをつけて起動すると初期設定のウィザードが出るのでそれに従う。
  途中でキャンセルして手動で設定も可能であるが、BS/CSについては少なくとも最初は
  ウィザードを使用しないと以後のチューニング/チャンネルスキャンがうまく行かいかない。
- UIのモードをExpertにしないと、BCASのデスクランブル設定を有効にできない
- 設定中に変更した場合は、タブを移る前にsaveしないと変更が失われる。
- 事前に地デジ・衛星のチャンネル設定ファイル(のプロトタイプ)を用意する。
  TSを特定するだけの最低限の情報(伝送システム{ISDB-S/T}, 周波数, [ts-ID])だけ記述すれば、
  TS名やチャンネル名など自動的にスキャンして登録される。
  地デジ用は、./data/dvb-conf/isdb-t/jp-Setoを参照し、
  自分の地域で使用されている周波数に変更してjp-Xxxxの名前で保存しておく。
  (sudo make installした場合は保存dirを、Config->General->DVB scan file pathに設定の
  必要があるかも)。 衛星は同isdb-s/BSAT-3a-110.0Eに既に定義済み。必要ならば修正して使用。

設定の参考
------------

* `-C`オプションを付けてウィザードを起動し、初期設定

0. Web Interface (:= English), EPG Language (:= Japanese) の言語を設定。

1. ユーザ関係/アクセス許可の設定.
    - 許可ネットワーク127.0.0.1 (後でローカルサイト上のKodiからアクセスする予定)
    - adminユーザとパスワード設定
    - 一般ユーザ作成

2. チューナー毎の設定
    - IPTV以外のチューナーにNetwork typeを設定 (ISDB-{S,T} network)
    - Pre-defined networksを設定 (jp-Seto, BSAT-3a-110.0E) スキャンの終了を待つ

3. サービス -> チャンネルの設定
    - 日本では１つのチャンネルが複数の電波(サービス)で放送されているケースは少ないが、
      tvheadendではサービスとチャンネルは区別されているので、1:1対応でもmappingが必要。
    - ただしスキャンされたサービスはデータサービスや臨時サービス、予備？なども含まれているので、
      ここではMap all servicesはチェックしないで、後程手動で選択してmapする。

* 手動での追加設定

0. adminでhttp://127.0.0.1:9981/にログインし、Configurationタブに移動
1. General->Base->User interface level をexpertに設定 (BCASの設定が済むまで) ->Save
2. DVB Inputs->Networks
    - 各ネットワークを選択->Edit->Character set をARIB-STD-B24に変更 -> Save
3. DVB Inputs->Services で必要なサービス(チャンネル)をMapする
    - リストから(Shift/Control クリックで)複数選択して、
      Map Selected -> Map selected services -> Map services
    - 同名のサービスが複数リストアップされるが、基本的にサービスID最小のサービスがメイン

4. Channel/EPG
    - Channelsタブで各チャンネルを選択、Edit->Use EPG running state: enable, Number設定等
    - EPG Grabber->Periodically save EPG to disk: 3
    - EPG Grabber Modules でEIT: DVB Grabber以外のモジュールをEnabledを外す->Save

5. Recording->Digital Video Recorder Profiles->(default profile)
    - Use EPG running stateをチェック、Recording system path設定他

6. CAs
    - Add -> Type:BCAS(MULTI2) -> Enable, Client name:bcas -> Create -> Save


WebUIでの確認
------------

- 再生: Configuration->Channel/EPG->Channelsから 先頭のPlayリンクを再生
  (プレイリスト形式。URLから/play/を除くと直接ストリーム再生)
  Config->DVB Inputs->{Muxes, Services}->Play 
- 録画: Electric Program Guide->Details->Record


以下、オリジナルのReadme.mdが続く。

Tvheadend
========================================
(c) 2006 - 2016 Tvheadend Foundation CIC

Status
------

[![Build Status](https://travis-ci.org/tvheadend/tvheadend.svg?branch=master)](https://travis-ci.org/tvheadend/tvheadend)

[![Download](https://api.bintray.com/packages/tvheadend/deb/tvheadend/images/download.svg)](https://bintray.com/tvheadend/deb/tvheadend/)

[![Coverity Scan](https://scan.coverity.com/projects/2114/badge.svg)](https://scan.coverity.com/projects/2114)

What it is
----------

Tvheadend is a TV streaming server and digital video recorder.

It supports the following inputs:

  * DVB-C(2)
  * DVB-T(2)
  * DVB-S(2)
  * ATSC
  * SAT>IP
  * HDHomeRun
  * IPTV
    * UDP
    * HTTP

It supports the following outputs:

  * HTTP
  * HTSP (own protocol)
  * SAT>IP

How to build for Linux
----------------------

First you need to configure:

	$ ./configure

If any dependencies are missing the configure script will complain or attempt
to disable optional features.

Build the binary:

	$ make

After build, the binary resides in `build.linux` directory.

Thus, to start it, just type:

	$ ./build.linux/tvheadend

Settings are stored in `$HOME/.hts/tvheadend`.

How to build for OS X
---------------------

Same build procedure applies to OS X.
After build, the binary resides in `build.darwin` directory.

Only network sources (IPTV, SAT>IP) are supported on OS X.
There is no support for DVB USB sticks and PCI cards.
Transcoding is currently not supported.

Packages
--------

The latest official packages can be downloaded from:

  * Debian/Ubuntu : https://bintray.com/tvheadend/deb

Further information
-------------------

For more information about building, including generating packages, please visit:
> https://tvheadend.org/projects/tvheadend/wiki/AptRepository
> https://tvheadend.org/projects/tvheadend/wiki/Building  
> https://tvheadend.org/projects/tvheadend/wiki/Packaging  
> https://tvheadend.org/projects/tvheadend/wiki/Git
> https://tvheadend.org/projects/tvheadend/wiki/Internationalization
