cmake_minimum_required(VERSION 3.22)
set(base [=[
"file":"@SOURCE@/tests/workshop_block.stl",
"transformColumnMajor":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],
"machine":{"rotary":[{"role":"table","direction":[1,0,0],"origin":[0,0,0],"sign":1}]},
"stock":{"shape":"box","min":[0,-4,-4],"max":[30,4,4]},
"retract":56,
"workflow":{"faces":2,"axes":4,"post":"shanlong4",
"tools":[{"diameter":4,"tipRadius":0,"overhang":10,"flute":5,"holderDiameter":6},
         {"diameter":2,"tipRadius":1,"overhang":12,"flute":5,"holderDiameter":6}],
"paths":[{"toolIndex":0,"faceAngle":0,"step":2,"allowance":0.5,"axes":4,"active":true},
         {"toolIndex":0,"faceAngle":180,"step":2,"allowance":0.5,"axes":4,"active":true},
         {"toolIndex":1,"faceAngle":-1,"step":3,"allowance":0,"axes":4,"active":true}]}
]=])
string(CONFIGURE "${base}" spec @ONLY)
string(REPLACE "\n" " " spec "${spec}")
set(job "${CMAKE_CURRENT_BINARY_DIR}/worker-integration.json")
foreach(op IN ITEMS plan-4x wrap-stl)
    file(WRITE "${job}" "{\"operation\":\"${op}\",${spec}}\n")
    execute_process(COMMAND "${WORKER}" INPUT_FILE "${job}" OUTPUT_VARIABLE response RESULT_VARIABLE exit_code)
    if(NOT exit_code EQUAL 0)
        message(FATAL_ERROR "Worker ${op} returned ${exit_code}: ${response}")
    endif()
    string(JSON ok GET "${response}" ok)
    if(NOT ok)
        message(FATAL_ERROR "Worker ${op} failed: ${response}")
    endif()
    if(op STREQUAL "plan-4x")
        string(JSON cuts GET "${response}" cuts)
        string(JSON nc GET "${response}" nc)
        string(JSON points LENGTH "${response}" segments)
        if(cuts LESS 1 OR points LESS 2 OR nc MATCHES "(^|\n| )B[-+0-9]" OR NOT nc MATCHES "G00 Z56.000\nG00 A")
            message(FATAL_ERROR "Invalid preview or NC output")
        endif()
        message(STATUS "Worker 4X generated ${cuts} indexed/raster points")
    else()
        string(JSON n LENGTH "${response}" surface)
        if(NOT n EQUAL 18432)
            message(FATAL_ERROR "Mismatched Wrap display mesh length: ${n}")
        endif()
        message(STATUS "Worker Wrap generated ${n} display vertices")
    endif()
endforeach()
file(REMOVE "${job}")
