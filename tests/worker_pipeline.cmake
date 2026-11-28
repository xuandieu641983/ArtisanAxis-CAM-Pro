cmake_minimum_required(VERSION 3.22)
string(RANDOM LENGTH 10 ALPHABET 0123456789abcdef run_id)
set(dir "${CMAKE_CURRENT_BINARY_DIR}/pipeline-check-${run_id}")
file(MAKE_DIRECTORY "${dir}")
set(common [=[
"file":"@SOURCE@/tests/spiral_cylinder.stl",
"transformColumnMajor":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],
"machine":{"workOffsetMachine":[0,0,-100],"offsetConfirmed":true,"swingarmMm":232.918,"rtcpMode":"G43.4",
"limits":{"Z":[-450,200],"B":[-20,20]},"rotary":[{"direction":[1,0,0],"origin":[0,0,0],"sign":1,"role":"table"},
{"direction":[0,1,0],"origin":[0,0,0],"sign":1,"role":"head"}]},
"stock":{"shape":"box","min":[0,-6.6,-6.6],"max":[19,6.6,6.6]},"retract":61
]=])
set(passes [=[
"faces":4,"tools":[{"tipRadius":0,"diameter":2,"flute":12,"overhang":18,"holderDiameter":4},
{"tipRadius":0.4,"diameter":0.8,"flute":12,"overhang":16,"holderDiameter":2}],
"paths":[
{"active":true,"toolIndex":0,"name":"Face 0","faceAngle":0,"step":2,"allowance":0.15,"feed":450},
{"active":true,"toolIndex":0,"name":"Face 180","faceAngle":180,"step":2,"allowance":0.15,"feed":450},
{"active":true,"toolIndex":0,"name":"Face 90","faceAngle":90,"step":2,"allowance":0.15,"feed":450},
{"active":true,"toolIndex":0,"name":"Face 270","faceAngle":270,"step":2,"allowance":0.15,"feed":450},
{"active":true,"toolIndex":1,"name":"Semi","faceAngle":-1,"step":3,"allowance":0.15,"feed":550},
{"active":true,"toolIndex":1,"name":"Finish","faceAngle":-1,"step":3,"allowance":0,"feed":700,"bMin":-12,"bMax":12}
]
]=])
string(CONFIGURE "${common}" common @ONLY)
function(run operation axes post nc)
    string(REPLACE "\n" " " body "{\"operation\":\"${operation}\",\"ncOutput\":\"${nc}\",\"ncFile\":\"${nc}\",${common},\"workflow\":{\"axes\":${axes},\"post\":\"${post}\",${passes}}}")
    set(request "${dir}/request-${axes}.json")
    file(WRITE "${request}" "${body}\n")
    execute_process(COMMAND "${WORKER}" INPUT_FILE "${request}" OUTPUT_VARIABLE json ERROR_VARIABLE progress RESULT_VARIABLE result TIMEOUT 140)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${operation} ${axes}X failed (${result}): ${json}")
    endif()
    if(NOT progress MATCHES [=["percent":100]=])
        message(FATAL_ERROR "${operation} ${axes}X did not report completed progress")
    endif()
    set(response "${json}" PARENT_SCOPE)
endfunction()

run("wrap-stl" 4 shanlong4 "")
string(JSON surface LENGTH "${response}" surface)
if(surface LESS 1000)
    message(FATAL_ERROR "Expected a mesh-derived drive surface with enough display vertices")
endif()
foreach(axes 4 5)
    if(axes EQUAL 4)
        set(post shanlong4)
    else()
        set(post cm600)
    endif()
    set(nc "${dir}/${axes}x.nc")
    run("plan-motion" ${axes} "${post}" "${nc}")
    string(JSON verified GET "${response}" verified)
    string(JSON frames LENGTH "${response}" playback)
    string(JSON stages LENGTH "${response}" stages)
    string(JSON blocks GET "${response}" blocks)
    if(NOT verified OR NOT EXISTS "${nc}" OR frames LESS 10 OR NOT stages EQUAL 3)
        string(JSON diagnostics GET "${response}" diagnostics)
        message(FATAL_ERROR "${axes}X expected verified NC, playback and three stock stages. Diagnostics: ${diagnostics}")
    endif()
    foreach(stage RANGE 0 2)
        string(JSON group GET "${response}" stages ${stage} group)
        string(JSON gouge GET "${response}" stages ${stage} gouge)
        if(NOT group EQUAL stage OR NOT gouge EQUAL 0)
            message(FATAL_ERROR "${axes}X stage ${stage} is out of order or cuts model voxels")
        endif()
    endforeach()
    file(READ "${nc}" program)
    foreach(angle 0 90 180 270)
        if(NOT program MATCHES "G01 [^\n]* A${angle}[.]000")
            message(FATAL_ERROR "${axes}X raster face ${angle} has no cutting motion")
        endif()
    endforeach()
    foreach(feed 450 550 700)
        if(NOT program MATCHES " F${feed}[.]000")
            message(FATAL_ERROR "${axes}X missing phase feed F${feed}")
        endif()
    endforeach()
    if(axes EQUAL 5)
        if(NOT program MATCHES "G43[.]4 H1" OR NOT program MATCHES "G49\nM5\nM30")
            message(FATAL_ERROR "CM600 5X must bracket the program with RTCP commands")
        endif()
    elseif(program MATCHES " B[-0-9]" OR program MATCHES "G43[.]4")
        message(FATAL_ERROR "CM600 4X includes unwanted B or RTCP")
    endif()
    run("verify-nc" ${axes} "${post}" "${nc}")
    string(JSON readBack GET "${response}" blocks)
    string(JSON reopened GET "${response}" verified)
    if(NOT reopened OR NOT readBack EQUAL blocks)
        message(FATAL_ERROR "${axes}X NC failed readback, expected ${blocks} blocks")
    endif()
    message(STATUS "${axes}X ${blocks} blocks, four faces, three stock snapshots, zero gouged voxels")
endforeach()
file(REMOVE_RECURSE "${dir}")
