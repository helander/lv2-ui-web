rm -fr jalv*
export UI_BRIDGE_NAME=b_synth_1
plugin=http://gareus.org/oss/lv2/b_synth
pw-jack jalv -d -n $UI_BRIDGE_NAME -s -t -U http://helander.network/lv2uiweb/bsynth $plugin
