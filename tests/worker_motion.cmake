cmake_minimum_required(VERSION 3.22)
set(spec [=[
"file":"@SOURCE@/tests/spiral_cylinder.stl",
"transformColumnMajor":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],
"machine":{"workOffsetMachine":[0,0,-100],"limits":{"Z":[-450,200]},"rotary":[{"direction":[1,0,0],"origin":[0,0,0],"sign":1,"role":"table"}]},
"stock":{"shape":"cylinder","min":[0,-6.2,-6.2],"max":[19,6.2,6.2]},
"retract":56.2,
"workflow":{"axes":4,"post":"shanlong4","faces":2,
"tools":[{"tipRadius":1,"diameter":2,"flute":12,"overhang":18,"holderDiameter":5}],
"paths":[{"active":true,"toolIndex":0,"faceAngle":-1,"step":3,"allowance":0}]}
]=])
string(CONFIGURE "${spec}" cfg @ONLY)
string(REPLACE "\n" " " cfg "${cfg}")
set(job "${CMAKE_CURRENT_BINARY_DIR}/simultaneous-job.json")
set(nc "${CMAKE_CURRENT_BINARY_DIR}/simultaneous-verified.nc")
function(run jobBody)
    file(WRITE "${job}" "${jobBody}\n")
    execute_process(COMMAND "${WORKER}" INPUT_FILE "${job}" OUTPUT_VARIABLE out RESULT_VARIABLE result TIMEOUT 90)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Worker ${result}: ${out}")
    endif()
    string(JSON ok GET "${out}" verified)
    if(NOT ok)
        string(JSON diagnostics GET "${out}" diagnostics)
        message(FATAL_ERROR "Failed: ${diagnostics}")
    endif()
    set(resultJson "${out}" PARENT_SCOPE)
endfunction()
run("{\"operation\":\"plan-motion\",${cfg}}")
string(JSON program GET "${resultJson}" nc)
string(JSON blocks GET "${resultJson}" blocks)
string(JSON stages LENGTH "${resultJson}" stages)
if(program STREQUAL "" OR NOT stages EQUAL 1)
    message(FATAL_ERROR "Expected verified NC and one finish snapshot")
endif()
file(WRITE "${nc}" "${program}")
run("{\"operation\":\"verify-nc\",\"ncFile\":\"${nc}\",${cfg}}")
string(JSON readBack GET "${resultJson}" blocks)
if(NOT readBack EQUAL blocks)
    message(FATAL_ERROR "Reopen block count differs: ${readBack} / ${blocks}")
endif()
message(STATUS "${readBack} simultaneous blocks verified directly from saved NC")
file(REMOVE "${job}" "${nc}")
