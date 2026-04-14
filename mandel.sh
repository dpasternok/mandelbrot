#!/usr/bin/env bash

cx=-0.5
cy=0
scale=3
max_iter=200
vert_scale=2

enable_raw() {
    stty -echo -icanon time 0 min 1
}

disable_raw() {
    stty sane
}

get_size() {
    rows=$(tput lines)
    cols=$(tput cols)
}

draw() {
    get_size
    tput clear
    ./mandel "$cx" "$cy" "$scale" "$rows" "$cols" "$max_iter" "$vert_scale"
    printf "\ncenter=(%.6f, %.6f) scale=%.6f iter=%d\n" "$cx" "$cy" "$scale" "$max_iter"
    printf "[arrows move, +/- zoom, q exit]\n"
}

main() {
    enable_raw
    trap 'disable_raw; tput cnorm; tput clear; exit' INT TERM EXIT

    draw

    while true; do
        read -rsn1 k
        case "$k" in
            q) break ;;
            =)
                scale=$(echo "$scale*0.7" | bc -l)
                max_iter=$((max_iter+5))
                draw
                ;;
            -)
                scale=$(echo "$scale/0.7" | bc -l)
                ((max_iter>20)) && max_iter=$((max_iter-5))
                draw
                ;;
            $'\e')
                read -rsn2 k2
                case "$k2" in
                    "[A") cy=$(echo "$cy - $scale*0.1" | bc -l); draw ;;
                    "[B") cy=$(echo "$cy + $scale*0.1" | bc -l); draw ;;
                    "[C") cx=$(echo "$cx + $scale*0.1" | bc -l); draw ;;
                    "[D") cx=$(echo "$cx - $scale*0.1" | bc -l); draw ;;
                esac
                ;;
        esac
    done

    disable_raw
    tput clear
}

main