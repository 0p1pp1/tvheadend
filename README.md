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

        $ tvheadend --nosatip --nobat

`make install`をしていないなら、`./build.linux/tvheadend --nosatip --nobat`

(使用するDVBアダプタを限定したいなら、`--adapters 4`等を使用。 `--help`を参照)


設定上の注意
----------

基本的には[オジリナル版での設定](http://docs.tvheadend.org/configure_tvheadend/)で、
http://127.0.0.1:9981/にアクセスして設定する。
幾つか解りにくい/気付きにくい設定項目があるので注意。

- `-C`オプションをつけて起動すると初期設定のウィザードが出るのでそれに従うか、
  (キャンセルして)手動で言語設定・ユーザ設定・ネットワーク設定・チャンネル設定等を行う。
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

0. Config->General
    - User interface levelをexpertに設定 (BCASの設定が済むまで)

1. Config->User でユーザ関係/アクセス許可の設定.
    - 許可ネットワーク127.0.0.1 (後でローカルサイト上のKodiからアクセスする予定)
    - adminパスワード設定
    - 一般ユーザ作成、余計な権限削除

2. Config->DVB Inputs->TV adapters
    - enable, Name, over-the-air EPG, initial scan, idle scanを設定
    - [ISDB-T] Networksは、後で隣のNetworksタブで地デジ局全体の伝送ネットワークを作成してから、
      戻ってきてそれをチェック (ドロップダウンやクリックで簡単にチェックが外れてしまうので注意)
    - [ISDB-S] SatConfig: Universal LNB only
    - [ISDB-S] adapterの下のLNBを設定. 
        * ISDB-Tの場合と同様にNetworksのチェックボックスを(後で)設定
        * Tune before DiseqC, Full DiseqC等をオフに
        * Turn off LNB when idleはオンにしてもok

3. Config->DVB Inputs->Networks
    - +Add->Type: ISDB-T -> Network name, Pre-defined muxes: jp-Xxx,
       Network discovery: on, Character set: ARIB-STD-B24
    - +Add->Type: ISDB-S -> Network name, Pre-defined muxes: >110.0E:BSAT-3a,
       Orbital position:110E, Network discovery: on, Charset: ARIB-STD-B24
    - Config->DVB Inputs->TV adaptersに戻って各アダプタ/LNBのNetworksを設定

4. Config->DVB Inputs->Muxes にスキャンされたTSがリストアップされるのを確認

   statusがOKなら隣のServicesにサービス(チャンネル)がリストアップされる.

5. Config->DVB Inputs->Services
    - Map all 又は、必要なチャンネルを選んで Map Selected -> Map services
    - Config -> Channel/EPG -> Channelsにリストされたらok
6. Config -> Channel/EPG -> Channels
    - Edit->Use EPG running state: enable
    - EPG Grabber->Periodically save EPG to disk: 3
    - Config->Channel/EPG->EPG Grabber Modules でEIT: DVB Grabberをenable(他は不要)

7. Config->Recording->Digital Video Recorder Profiles->Use EPG running state,
    Recording system path設定

8. Config->CAs
    - "+Add" -> Type:BCAS(MULTI2) -> Enable, Client name -> +Create -> Save


WebUIでの確認
------------

- 再生: Config->Channel/EPG->Channelsから 先頭のPlayリンクを再生
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
