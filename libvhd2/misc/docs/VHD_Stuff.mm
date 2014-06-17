<map version="0.9.0">
<!-- To view this file, download free mind mapping software FreeMind from http://freemind.sourceforge.net -->
<node CREATED="1340707323927" ID="ID_444235007" MODIFIED="1340821123482" TEXT="VHD stuff">
<node CREATED="1340707357583" ID="ID_392646047" MODIFIED="1340724007906" POSITION="right" STYLE="bubble" TEXT="Requirements">
<node CREATED="1340707379032" ID="ID_48212378" MODIFIED="1340707388930" TEXT="implement TRIM">
<icon BUILTIN="help"/>
</node>
<node CREATED="1340724013196" ID="ID_480441502" MODIFIED="1340821529806" TEXT="Encryption ?">
<arrowlink DESTINATION="ID_197328704" ENDARROW="Default" ENDINCLINATION="172;-176;" ID="Arrow_ID_60933829" STARTARROW="None" STARTINCLINATION="589;-262;"/>
<arrowlink DESTINATION="ID_1834015655" ENDARROW="Default" ENDINCLINATION="314;0;" ID="Arrow_ID_1384356021" STARTARROW="None" STARTINCLINATION="314;0;"/>
</node>
</node>
<node CREATED="1340714660609" ID="ID_131858543" MODIFIED="1340714678220" POSITION="left" STYLE="bubble" TEXT="Testing"/>
<node CREATED="1340715477244" ID="ID_414047440" MODIFIED="1340729719095" POSITION="right" STYLE="bubble" TEXT="Questions">
<node CREATED="1340723909119" ID="ID_1834015655" MODIFIED="1341237746194">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      VHD encryption
    </p>
    <p>
      Currently it looks that it is implemented in /Source/blktap/drivers/block-crypto.c
    </p>
    <p>
      See void vhd_crypto_decrypt(...)
    </p>
    <ul>
      <li>
        How is it done ? Access to VHD internal structures ?
      </li>
      <li>
        What is the new API is going to be ?
      </li>
    </ul>
    <p>
      Some info here:
    </p>
    <p>
      http://wiki.cam.xci-test.com/index.php/Disk_encryption
    </p>
  </body>
</html>
</richcontent>
<icon BUILTIN="help"/>
</node>
<node BACKGROUND_COLOR="#ffff00" CREATED="1340728249520" ID="ID_1599614402" MODIFIED="1340729492233" TEXT="Existing implementation">
<node CREATED="1340723970092" ID="ID_235231130" MODIFIED="1340821361457">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      How many VHDs in chain usually ?
    </p>
    <p>
      ~ 10, usually no more
    </p>
  </body>
</html>
</richcontent>
<icon BUILTIN="button_ok"/>
</node>
<node CREATED="1340728316371" ID="ID_1715337051" MODIFIED="1340728525428" TEXT="What is &quot;disabled&quot; VHD with &quot;poison cookie&quot;?"/>
<node CREATED="1340726981471" ID="ID_845324950" MODIFIED="1340727077377">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      vhdlib seems to implement its own data caching mechanism via vhd_block_vector
    </p>
    <p>
      What will happen if there will be just normal file reads (cached by system) ?
    </p>
  </body>
</html></richcontent>
<icon BUILTIN="help"/>
</node>
<node CREATED="1340800452665" ID="ID_1048221222" MODIFIED="1340805254127">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      What is &quot;BatMap&quot;?
    </p>
    <p>
      Something Proprietary ?
    </p>
    <p>
      VHD_BATMAP_COOKIE = &quot;tdbatmap&quot;
    </p>
    <p>
      Looks to be the same size as BAT, used for blocks moving (indirection ?) inside vhd for some tools ?
    </p>
  </body>
</html></richcontent>
<arrowlink DESTINATION="ID_457030551" ENDARROW="Default" ENDINCLINATION="561;0;" ID="Arrow_ID_674606307" STARTARROW="Default" STARTINCLINATION="561;0;"/>
</node>
</node>
<node BACKGROUND_COLOR="#00cccc" CREATED="1340728276968" ID="ID_930025205" MODIFIED="1340728511984" TEXT="New Implementation">
<node CREATED="1340715494264" ID="ID_1611519562" MODIFIED="1340821416862">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      VHD files and their structures caching on a disk
    </p>
    <ul>
      <li>
        Is it OK to use normal Read caching
      </li>
      <li>
        Is it OK to use Write caching (Should be API to flush everything)
      </li>
      <li>
        WB - caching of some internal VHD structures, like sectors bitmaps ?
      </li>
      <li>
        Crash situations with dirty cached data lost (need for transactional operations ?)
      </li>
      <li>
        Memory constraints for internal caches
      </li>
    </ul>
  </body>
</html>
</richcontent>
<icon BUILTIN="help"/>
</node>
<node CREATED="1340821230749" ID="ID_464519392" MODIFIED="1340821706347">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <ul>
      <li>
        no need to make WB caching of the metadata
      </li>
      <li>
        ? read cachig of the metatdata ?
      </li>
      <li>
        allow opening VHDs in DIRECT and Normal modes (FS caching)
      </li>
    </ul>
  </body>
</html>
</richcontent>
</node>
</node>
<node CREATED="1340728351748" ID="ID_1491898751" MODIFIED="1340729728997">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      How do we tell that the new implementation is better ?
    </p>
    <ul>
      <li>
        Performance (testing)
      </li>
      <li>
        RAM consumption
      </li>
      <li>
        disk IO
      </li>
    </ul>
  </body>
</html></richcontent>
<icon BUILTIN="help"/>
</node>
<node CREATED="1340729493113" HGAP="25" ID="ID_937690879" MODIFIED="1340729722165" VSHIFT="39">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      Implications of the Kernel/User interaction for Dom0/DomU. I.e vhd files handling is implemented in user space (at least old/ new API).
    </p>
    <p>
      While the data required in kernel space for guests ??
    </p>
    <p>
      Any optimisations here that can be done ?
    </p>
  </body>
</html></richcontent>
<icon BUILTIN="help"/>
</node>
<node CREATED="1340800298334" ID="ID_457030551" MODIFIED="1340805254127">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      VHD Journal ?? What's it ?
    </p>
    <p>
      used by VhdUtils to deal with VHD geometry changes ??
    </p>
  </body>
</html></richcontent>
</node>
</node>
<node CREATED="1340821124193" ID="ID_323887654" MODIFIED="1340821147193" POSITION="left" STYLE="bubble" TEXT="James&apos;s  ideas">
<node CREATED="1340821153802" ID="ID_92109041" MODIFIED="1340821206005">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      'pure' sector bitmap; where whole bitmap is set to '1' or to '0'
    </p>
  </body>
</html>
</richcontent>
</node>
<node CREATED="1340821440588" ID="ID_197328704" MODIFIED="1340821529806">
<richcontent TYPE="NODE"><html>
  <head>
    
  </head>
  <body>
    <p>
      New encryption:
    </p>
    <p>
      - encrypt data, but not metadata
    </p>
    <p>
      - ability to use different keys for the head and following VHDs
    </p>
  </body>
</html>
</richcontent>
</node>
</node>
</node>
</map>
