combo_name=SalamanderGrandPianoV3
plugin=http://spectmorph.org/plugins/liquidsfz
ui=http://helander.network/lv2uiweb/liquidsfz
export SFZ_FILEPATH="/home/lehswe/compose/soundcan/SFZ/SalamanderGrandPianoV3_48khz24bit/${combo_name}.sfz"
export HTTP_PORT=2556
pw-jack -p 256 jalv -d -n $combo_name -s -t -U $ui  $plugin
